# Integration tests

Placeholder for the three-client smoke test described in `AGENTS.md`.

Planned scenario:

1. Start one server on a known port.
2. Start three clients with separate share folders.
3. Verify `find -s`, `find -d`, and `request S H` against a shared file.
4. Remove one share folder while the client is running and confirm all processes survive.
