/**
 * Overlay Bridge
 *
 * Two display modes:
 *   1. Game Mode (gamescope): Opens screens via Steam's built-in browser
 *      using steam://openurl/http://localhost:PORT/screen
 *   2. Desktop Mode: Opens screens via xdg-open in the default browser
 *
 * Communication: HTTP server + WebSocket for real-time bidirectional messaging.
 * The web pages are served by this module and use the same JSON protocol
 * as the original SDL2 IPC.
 */

import { EventEmitter } from 'node:events';
import { createServer } from 'node:http';
import { execFile } from 'node:child_process';
import { WebSocketServer } from 'ws';

const OVERLAY_PORT = 3001;

export class OverlayBridge extends EventEmitter {

    constructor() {
        super();
        this._httpServer = null;
        this._wss = null;
        this._ws = null;
        this._currentScreen = null;
        this._screenData = {};
        this._lastHeartbeat = 0;
        this._heartbeatTimer = null;
        this._lastReopen = 0;
        this._reopenCount = 0;
        this._reopenTimer = null;
    }

    // ── Lifecycle ──────────────────────────────────────────────

    async start() {
        if (this._httpServer) return;

        var self = this;

        return new Promise(function (resolve, reject) {
            self._httpServer = createServer(function (req, res) {
                self._handleHttp(req, res);
            });

            self._wss = new WebSocketServer({ server: self._httpServer });

            self._wss.on('connection', function (ws) {
                self._ws = ws;
                self._lastHeartbeat = Date.now();
                self._reopenCount = 0;
                console.log('Overlay WebSocket connected');

                // Start heartbeat monitor — detects closure faster than TCP close
                self._startHeartbeatMonitor();

                ws.on('message', function (data) {
                    try {
                        var msg = JSON.parse(data.toString());
                        if (msg.event === 'heartbeat') {
                            self._lastHeartbeat = Date.now();
                            return;
                        }
                        if (msg.event === 'page-closing') {
                            // Client is being closed — schedule immediate re-open
                            self._scheduleReopen('beforeunload');
                            return;
                        }
                        self._handleMessage(msg);
                    } catch (_e) {
                        console.warn('Invalid overlay WebSocket message');
                    }
                });

                ws.on('close', function () {
                    if (self._ws === ws) {
                        self._ws = null;
                        self._scheduleReopen('ws-close');
                    }
                });

                // Send current screen state if any
                if (self._currentScreen) {
                    ws.send(JSON.stringify(self._screenData));
                }
            });

            self._httpServer.listen(OVERLAY_PORT, '127.0.0.1', function () {
                console.log('Overlay web server on http://127.0.0.1:' + OVERLAY_PORT);
                resolve();
            });

            self._httpServer.on('error', reject);
        });
    }

    async stop() {
        this._stopHeartbeatMonitor();
        if (this._reopenTimer) {
            clearTimeout(this._reopenTimer);
            this._reopenTimer = null;
        }
        if (this._ws) {
            this._ws.close();
            this._ws = null;
        }
        if (this._wss) {
            this._wss.close();
            this._wss = null;
        }
        if (this._httpServer) {
            this._httpServer.close();
            this._httpServer = null;
        }
        this._currentScreen = null;
    }

    isAvailable() {
        return this._httpServer !== null;
    }

    // ── Screen Commands ─────────────────────────────────────────

    showPairingScreen(params) {
        this._showScreen('pairing', {
            screen: 'pairing',
            pin: params.pin,
            qrData: params.qrData || '',
            message: params.message || '',
        });
    }

    showChildSelector(children) {
        this._showScreen('selector', {
            screen: 'selector',
            children: (children || []).map(function (c) {
                return { id: c.id, name: c.name };
            }),
        });
    }

    showPinEntry(params) {
        this._showScreen('pin', {
            screen: 'pin-entry',
            childId: params.childId,
            childName: params.childName,
            isParent: !!params.isParent,
            maxDigits: params.maxDigits || 4,
        });
    }

    sendPinResult(params) {
        this._sendWs({
            screen: 'pin-result',
            success: !!params.success,
            attemptsRemaining: params.attemptsRemaining || 0,
            lockedOut: !!params.lockedOut,
            lockoutSeconds: params.lockoutSeconds || 0,
        });
    }

    showLockScreen(params) {
        this._showScreen('lock', {
            screen: 'lock',
            reason: params.reason || 'Screen time is up',
            childName: params.childName || '',
            childId: params.childId || 0,
            activityId: params.activityId || 0,
        });
    }

    showWarning(params) {
        this._showScreen('warning', {
            screen: 'warning',
            activity: params.activity || '',
            activityId: params.activityId || 0,
            remaining: params.remaining || 0,
            level: params.level || 'info',
        });
    }

    showRequestStatus(status) {
        this._sendWs({ screen: 'request-status', status: status });
    }

    showDenied() {
        this._sendWs({ screen: 'denied' });
    }

    dismiss() {
        this._currentScreen = null;
        this._screenData = {};
        this._reopenCount = 0;
        if (this._reopenTimer) {
            clearTimeout(this._reopenTimer);
            this._reopenTimer = null;
        }
        this._sendWs({ screen: 'dismiss' });
    }

    // ── Persistence (heartbeat + re-open) ─────────────────────

    _startHeartbeatMonitor() {
        var self = this;
        if (this._heartbeatTimer) clearInterval(this._heartbeatTimer);

        this._heartbeatTimer = setInterval(function () {
            // If we have a persistent screen but no heartbeat for 1.5s, re-open
            if (self._currentScreen && self._currentScreen !== 'warning') {
                var elapsed = Date.now() - self._lastHeartbeat;
                if (elapsed > 1500 && self._ws) {
                    console.log('Heartbeat lost (' + elapsed + 'ms), re-opening overlay');
                    self._ws = null;
                    self._scheduleReopen('heartbeat-timeout');
                }
            }
        }, 500);
    }

    _stopHeartbeatMonitor() {
        if (this._heartbeatTimer) {
            clearInterval(this._heartbeatTimer);
            this._heartbeatTimer = null;
        }
    }

    _scheduleReopen(reason) {
        var self = this;

        // Only re-open persistent screens
        if (!this._currentScreen || this._currentScreen === 'warning') return;

        // Debounce: don't re-open if we already did within 2 seconds
        var now = Date.now();
        if (now - this._lastReopen < 2000) return;

        // Give up after 5 rapid re-opens (user may be actively navigating)
        if (this._reopenCount >= 5) {
            console.log('Overlay re-open limit reached, backing off');
            // Reset after 30 seconds
            setTimeout(function () { self._reopenCount = 0; }, 30000);
            return;
        }

        // Cancel any pending re-open
        if (this._reopenTimer) clearTimeout(this._reopenTimer);

        var delay = reason === 'beforeunload' ? 200 : 500;

        this._reopenTimer = setTimeout(function () {
            if (self._currentScreen && self._currentScreen !== 'warning') {
                self._reopenCount++;
                self._lastReopen = Date.now();
                console.log('Re-opening ' + self._currentScreen + ' (reason: ' + reason + ', attempt: ' + self._reopenCount + ')');
                var url = 'http://127.0.0.1:' + OVERLAY_PORT + '/' + self._currentScreen;
                self._openUrl(url);
            }
        }, delay);
    }

    // ── Internal ───────────────────────────────────────────────

    _showScreen(screenName, data) {
        this._currentScreen = screenName;
        this._screenData = data;

        // Send to connected WebSocket client if any
        this._sendWs(data);

        // Open in Steam browser (Game Mode) or system browser (Desktop)
        var url = 'http://127.0.0.1:' + OVERLAY_PORT + '/' + screenName;
        this._openUrl(url);
    }

    _sendWs(message) {
        if (this._ws && this._ws.readyState === 1) {
            this._ws.send(JSON.stringify(message));
        }
    }

    _openUrl(url) {
        var self = this;
        var steamUrl = 'steam://openurl/' + url;

        // Primary: Steam CLI with steam://openurl/ (Game Mode)
        console.log('[overlay] trying steam: ' + steamUrl);
        execFile('steam', [steamUrl], { timeout: 5000 }, function (err) {
            if (err) {
                console.log('[overlay] steam failed: ' + (err.message || '').split('\n')[0]);
            }
        });

        // Fallback (3s): try known browsers directly (Desktop Mode)
        setTimeout(function () {
            if (self._ws) return;
            var browsers = ['firefox', 'chromium', 'google-chrome', 'xdg-open'];
            self._tryBrowsers(browsers, 0, url);
        }, 3000);
    }

    _tryBrowsers(browsers, index, url) {
        var self = this;
        if (index >= browsers.length || self._ws) return;

        var browser = browsers[index];
        console.log('[overlay] trying ' + browser + ': ' + url);
        execFile(browser, [url], { timeout: 5000 }, function (err) {
            if (err && !self._ws) {
                console.log('[overlay] ' + browser + ' failed');
                self._tryBrowsers(browsers, index + 1, url);
            }
        });
    }

    _handleMessage(msg) {
        if (!msg || !msg.event) return;

        switch (msg.event) {
            case 'child-selected':
                this.emit('child-selected', { childId: msg.childId });
                break;
            case 'pin-entered':
                this.emit('pin-entered', { childId: msg.childId, pin: msg.pin });
                break;
            case 'parent-selected':
                this.emit('parent-selected');
                break;
            case 'parent-pin-entered':
                this.emit('parent-pin-entered', { pin: msg.pin });
                break;
            case 'request-more-time':
                this.emit('request-more-time', {
                    activityId: msg.activityId,
                    duration: msg.duration,
                });
                break;
            case 'switch-child':
                this.emit('switch-child');
                break;
            default:
                console.warn('Unknown overlay event:', msg.event);
        }
    }

    // ── HTTP handler ───────────────────────────────────────────

    _handleHttp(req, res) {
        var path = req.url.split('?')[0];

        // API endpoint: get current screen data
        if (path === '/api/state') {
            res.writeHead(200, { 'Content-Type': 'application/json' });
            res.end(JSON.stringify(this._screenData || {}));
            return;
        }

        // Serve overlay pages
        res.writeHead(200, {
            'Content-Type': 'text/html; charset=utf-8',
            'Cache-Control': 'no-cache',
        });
        res.end(this._renderPage(path));
    }

    _renderPage(path) {
        var stateJson = JSON.stringify(this._screenData || {});
        var screenName = path.replace(/^\//, '') || 'index';

        return '<!DOCTYPE html>'
            + '<html><head>'
            + '<meta name="viewport" content="width=device-width,initial-scale=1">'
            + '<title>Allow2</title>'
            + '<style>' + OVERLAY_CSS + '</style>'
            + '</head><body>'
            + '<div id="app"></div>'
            + '<script>'
            + 'var INITIAL_STATE=' + stateJson + ';'
            + 'var SCREEN="' + screenName + '";'
            + OVERLAY_JS
            + '</script>'
            + '</body></html>';
    }
}

// ── Embedded CSS ───────────────────────────────────────────

var OVERLAY_CSS = ''
    + '* { margin:0; padding:0; box-sizing:border-box; }'
    + 'body { background:#14141E; color:#fff; font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,sans-serif;'
    + '  display:flex; align-items:center; justify-content:center; min-height:100vh; }'
    + '#app { text-align:center; padding:2rem; max-width:800px; width:100%; }'
    + '.pin-digits { font-size:4rem; letter-spacing:1.2rem; font-weight:700; color:#667eea; margin:1.5rem 0; }'
    + '.subtitle { color:#a0a0b0; font-size:1.1rem; margin:0.8rem 0; }'
    + '.child-list { display:flex; flex-wrap:wrap; gap:1.2rem; justify-content:center; margin:1.5rem 0; }'
    + '.child-btn { background:#2d3748; border:2px solid transparent; border-radius:12px; padding:1.2rem 2rem;'
    + '  color:#fff; font-size:1.1rem; cursor:pointer; min-width:140px; transition:all 0.2s; }'
    + '.child-btn:hover,.child-btn:focus { border-color:#667eea; background:#3d4758; outline:none; }'
    + '.child-btn .avatar { width:48px; height:48px; border-radius:50%; margin:0 auto 0.5rem;'
    + '  display:flex; align-items:center; justify-content:center; font-size:1.5rem; font-weight:700; }'
    + '.btn { background:#667eea; color:#fff; border:none; border-radius:8px; padding:0.8rem 2rem;'
    + '  font-size:1rem; cursor:pointer; margin:0.5rem; transition:all 0.2s; }'
    + '.btn:hover { background:#5a6fd6; }'
    + '.btn-secondary { background:#2d3748; }'
    + '.btn-secondary:hover { background:#3d4758; }'
    + '.lock-reason { font-size:2rem; font-weight:700; margin-bottom:0.5rem; }'
    + '.warning-bar { position:fixed; top:0; left:0; right:0; padding:0.8rem 1.5rem;'
    + '  display:flex; align-items:center; justify-content:space-between; z-index:999; }'
    + '.warning-bar.info { background:rgba(102,126,234,0.9); }'
    + '.warning-bar.urgent { background:rgba(246,173,85,0.9); }'
    + '.warning-bar.final,.warning-bar.countdown { background:rgba(252,129,129,0.9); }'
    + '.pin-input { display:flex; gap:0.8rem; justify-content:center; margin:1.5rem 0; }'
    + '.pin-dot { width:48px; height:48px; border-radius:50%; border:2px solid #667eea;'
    + '  display:flex; align-items:center; justify-content:center; font-size:1.5rem; background:#2d3748; }'
    + '.pin-dot.filled { background:#667eea; }'
    + '.pin-pad { display:grid; grid-template-columns:repeat(3,80px); gap:0.8rem; justify-content:center; margin:1rem 0; }'
    + '.pin-key { background:#2d3748; border:none; color:#fff; font-size:1.5rem; padding:1rem;'
    + '  border-radius:8px; cursor:pointer; transition:all 0.15s; }'
    + '.pin-key:hover { background:#3d4758; }'
    + '.duration-btns { display:flex; gap:1rem; justify-content:center; margin:1.5rem 0; }'
    + '.pending-dot { display:inline-block; width:10px; height:10px; border-radius:50%;'
    + '  background:#667eea; animation:pulse 1.5s ease-in-out infinite; }'
    + '@keyframes pulse { 0%,100%{opacity:0.4} 50%{opacity:1} }'
    + '.denied-msg { color:#fc8181; font-size:2rem; font-weight:700; }'
    + '.qr-placeholder { width:160px; height:160px; background:#2d3748; border-radius:12px;'
    + '  margin:1rem auto; display:flex; align-items:center; justify-content:center; color:#a0a0b0; }';

// ── Embedded JavaScript ────────────────────────────────────

var OVERLAY_JS = ''
    + '(function(){'
    + 'var app=document.getElementById("app");'
    + 'var ws;'
    + 'var state=INITIAL_STATE;'
    + 'var pinDigits="";'
    + ''
    + 'var hbTimer=null;'
    + 'function connect(){'
    + '  ws=new WebSocket("ws://"+location.host);'
    + '  ws.onopen=function(){'
    + '    if(hbTimer)clearInterval(hbTimer);'
    + '    hbTimer=setInterval(function(){send({event:"heartbeat"});},500);'
    + '  };'
    + '  ws.onmessage=function(e){'
    + '    var msg=JSON.parse(e.data);'
    + '    if(msg.screen==="dismiss"){window.close();return;}'
    + '    if(msg.screen==="pin-result"){handlePinResult(msg);return;}'
    + '    if(msg.screen==="request-status"){handleRequestStatus(msg);return;}'
    + '    state=msg;render();'
    + '  };'
    + '  ws.onclose=function(){if(hbTimer){clearInterval(hbTimer);hbTimer=null;}setTimeout(connect,2000);};'
    + '}'
    + ''
    + 'window.addEventListener("beforeunload",function(){'
    + '  if(ws&&ws.readyState===1)ws.send(JSON.stringify({event:"page-closing"}));'
    + '});'
    + 'document.addEventListener("visibilitychange",function(){'
    + '  if(document.hidden&&ws&&ws.readyState===1)ws.send(JSON.stringify({event:"page-closing"}));'
    + '});'
    + ''
    + 'function send(msg){if(ws&&ws.readyState===1)ws.send(JSON.stringify(msg));}'
    + ''
    + 'function render(){'
    + '  if(!state||!state.screen){app.innerHTML="<p class=subtitle>Waiting...</p>";return;}'
    + '  switch(state.screen){'
    + '    case"pairing":renderPairing();break;'
    + '    case"selector":renderSelector();break;'
    + '    case"pin-entry":renderPin();break;'
    + '    case"lock":renderLock();break;'
    + '    case"warning":renderWarning();break;'
    + '    default:app.innerHTML="<p class=subtitle>"+state.screen+"</p>";'
    + '  }'
    + '}'
    + ''
    + 'function renderPairing(){'
    + '  var pin=state.pin||"------";'
    + '  var digits=pin.split("").join(" ");'
    + '  app.innerHTML='
    + '    "<h1>Set Up Parental Controls</h1>"'
    + '    +"<div class=qr-placeholder>QR Code</div>"'
    + '    +"<p class=subtitle>"+(state.message||"Open the Allow2 app and enter this PIN")+"</p>"'
    + '    +"<div class=pin-digits>"+digits+"</div>"'
    + '    +"<p class=subtitle>PIN Code</p>";'
    + '}'
    + ''
    + 'function renderSelector(){'
    + '  var kids=state.children||[];'
    + '  var html="<h1>Who is playing?</h1><div class=child-list>";'
    + '  for(var i=0;i<kids.length;i++){'
    + '    var c=kids[i];'
    + '    var hue=(c.id*137)%360;'
    + '    var initial=c.name?c.name[0].toUpperCase():"?";'
    + '    html+="<button class=child-btn onclick=\\"selectChild("+c.id+")\\">"'
    + '      +"<div class=avatar style=\\"background:hsl("+hue+",60%,50%)\\">"+initial+"</div>"'
    + '      +c.name+"</button>";'
    + '  }'
    + '  html+="<button class=child-btn onclick=\\"selectParent()\\">"'
    + '    +"<div class=avatar style=\\"background:#4a5568\\">P</div>Parent</button>";'
    + '  html+="</div>";'
    + '  app.innerHTML=html;'
    + '}'
    + ''
    + 'window.selectChild=function(id){send({event:"child-selected",childId:id});};'
    + 'window.selectParent=function(){send({event:"parent-selected"});};'
    + ''
    + 'function renderPin(){'
    + '  var name=state.childName||"";'
    + '  var max=state.maxDigits||4;'
    + '  var dots="";'
    + '  for(var i=0;i<max;i++){'
    + '    dots+="<div class=\\"pin-dot"+(i<pinDigits.length?" filled":"")+"\\">"+('
    + '      i<pinDigits.length?"●":"")+"</div>";'
    + '  }'
    + '  var pad="";'
    + '  for(var n=1;n<=9;n++)pad+="<button class=pin-key onclick=\\"pinKey("+n+"\\">"+n+"</button>";'
    + '  pad+="<button class=pin-key onclick=\\"pinClear()\\">C</button>";'
    + '  pad+="<button class=pin-key onclick=\\"pinKey(0)\\">0</button>";'
    + '  pad+="<button class=pin-key onclick=\\"pinBack()\\">←</button>";'
    + '  app.innerHTML='
    + '    "<h1>Enter PIN</h1>"'
    + '    +"<p class=subtitle>"+name+"</p>"'
    + '    +"<div class=pin-input>"+dots+"</div>"'
    + '    +"<div class=pin-pad>"+pad+"</div>";'
    + '}'
    + ''
    + 'window.pinKey=function(n){'
    + '  var max=state.maxDigits||4;'
    + '  if(pinDigits.length>=max)return;'
    + '  pinDigits+=n;'
    + '  render();'
    + '  if(pinDigits.length===max){'
    + '    var ev=state.isParent?"parent-pin-entered":"pin-entered";'
    + '    send({event:ev,childId:state.childId,pin:pinDigits});'
    + '    pinDigits="";'
    + '  }'
    + '};'
    + 'window.pinClear=function(){pinDigits="";render();};'
    + 'window.pinBack=function(){pinDigits=pinDigits.slice(0,-1);render();};'
    + ''
    + 'function handlePinResult(msg){'
    + '  if(msg.success){app.innerHTML="<h1>✓</h1><p class=subtitle>Verified</p>";}'
    + '  else if(msg.lockedOut){app.innerHTML="<div class=denied-msg>Locked out</div>"'
    + '    +"<p class=subtitle>Try again in "+msg.lockoutSeconds+"s</p>";}'
    + '  else{app.innerHTML="<div class=denied-msg>Wrong PIN</div>"'
    + '    +"<p class=subtitle>"+msg.attemptsRemaining+" attempts remaining</p>";'
    + '    setTimeout(render,2000);}'
    + '}'
    + ''
    + 'function renderLock(){'
    + '  app.innerHTML='
    + '    "<div class=lock-reason>"+(state.reason||"Screen time is up")+"</div>"'
    + '    +"<p class=subtitle>"+(state.childName?state.childName+", your":"Your")+" daily screen time has been used up.</p>"'
    + '    +"<div style=\\"margin-top:2rem\\">"'
    + '    +"<button class=btn onclick=\\"requestTime()\\">Request More Time</button>"'
    + '    +"<button class=\\"btn btn-secondary\\" onclick=\\"switchChild()\\">Switch Child</button>"'
    + '    +"</div>";'
    + '}'
    + ''
    + 'window.requestTime=function(){'
    + '  app.innerHTML='
    + '    "<h1>Request More Time</h1>"'
    + '    +"<div class=duration-btns>"'
    + '    +"<button class=btn onclick=\\"doRequest(15)\\">15 min</button>"'
    + '    +"<button class=btn onclick=\\"doRequest(30)\\">30 min</button>"'
    + '    +"<button class=btn onclick=\\"doRequest(60)\\">1 hour</button>"'
    + '    +"</div>"'
    + '    +"<button class=\\"btn btn-secondary\\" onclick=\\"render()\\">← Back</button>";'
    + '};'
    + ''
    + 'window.doRequest=function(mins){'
    + '  send({event:"request-more-time",activityId:state.activityId||0,duration:mins});'
    + '};'
    + ''
    + 'window.switchChild=function(){send({event:"switch-child"});};'
    + ''
    + 'function handleRequestStatus(msg){'
    + '  if(msg.status==="pending"){'
    + '    app.innerHTML="<p class=subtitle>Waiting for parent approval...</p>"'
    + '      +"<div class=pending-dot></div>";'
    + '  }else if(msg.status==="denied"){'
    + '    app.innerHTML="<div class=denied-msg>Request denied</div>";'
    + '    setTimeout(render,3000);'
    + '  }else if(msg.status==="approved"){'
    + '    app.innerHTML="<h1>Approved!</h1>";'
    + '  }'
    + '}'
    + ''
    + 'function renderWarning(){'
    + '  var secs=state.remaining||0;'
    + '  var time=secs>=60?Math.ceil(secs/60)+" min":secs+"s";'
    + '  var level=state.level||"info";'
    + '  document.body.style.background="transparent";'
    + '  app.style.textAlign="left";'
    + '  app.innerHTML='
    + '    "<div class=\\"warning-bar "+level+"\\">"'
    + '    +"<span>"+(state.activity||"")+" — "+time+" remaining</span>"'
    + '    +(level!=="info"?"<button class=btn onclick=\\"doRequest(15)\\">Request More Time</button>":"")'
    + '    +"</div>";'
    + '}'
    + ''
    + 'connect();'
    + 'render();'
    + '})();';
