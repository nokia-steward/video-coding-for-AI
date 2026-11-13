# Security

## Reporting a vulnerability

If you believe you have found a security vulnerability in VCAI, **do not** open a public issue with exploit details.

Instead, report it privately to the project maintainers through the security contact channel defined by your organization for this repository (for example a dedicated security mailbox or private ticket queue your Nokia release team publishes).

Include:

- A description of the issue and affected components (`encoder_cli`, `rtp_server`, `libvcai`, transport paths, etc.)
- Steps to reproduce, if possible
- Impact assessment (confidentiality, integrity, availability)
- Your contact information for follow-up

We will acknowledge receipt when possible and coordinate on fix and disclosure timing.

## Supported versions

Security fixes are applied on actively maintained release branches as determined by the maintainers. Older branches may not receive patches.

## Safe testing

Do not run untrusted bitstreams, model weights, or config files against production networks without isolation. VCAI processes video and network input; treat demo configs and downloaded media as untrusted input.
