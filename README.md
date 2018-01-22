Allow2linux
===========

Placeholder for someone to port the allow2mac system over to linux.

It is intended to be a time-control system for linux-based OS systems.

# How it should work

The idea is to hook up a privileged helper and a user account level service that operates on the login and in the background with regular checks to the elevated helper (or whatever architecture makes sense for linux).

The login (terminal or window server) needs to validate login requests using the hooks provided to do so, and if failed, reject login.

A timer also regularly checks permission (polling) every sub-minute, perhaps 15 seconds or sothe privileged helper then polls the api endpoint.

There needs to be a control panel for admins on the linux box (and cmdline interface) to "pair" the system with the platform using the pairing routine. Then persist the pairing locally with the pairing ID. Then the interface allows a mapping of unix system accounts to Allow2 child accounts, so you know which child account to check quotas and access on.


## Example of how to use Allow2 to limit access on a multi-user account-based system.


It provides a concrete example of how to perform a full-featured integration with the Allow2 service via the open-source allow2deviceAPI.

The intention, by outsourcing, is to:

1. Accelerate development,
2. Provide an example for integration into device firmware and other application software
