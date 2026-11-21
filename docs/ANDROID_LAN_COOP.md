# Android LAN co-op (development preview)

This feature is being developed on the `feature/android-lan-coop` branch.

## Current test entry points

Both Android devices must be connected to the same Wi-Fi network and use the same game build and compatible game data.

On the host device, open the in-game developer console and run:

```text
lan_coop_host l01_escape/alife
```

On the joining device, run:

```text
lan_coop_join 192.168.1.100
```

Replace the example address with the host phone's local IPv4 address. The host log reports the TCP listening port. A non-default port can be supplied to the client as `/port=NUMBER`; an optional session password is passed as `/psw=PASSWORD` on the host and client.

## Scope and status

This is a development preview, not a finished co-op release. The current branch contains:
- an Android IPv4/TCP framed transport;
- a client/server handshake with protocol version, player ID, story-mode flags, and password rejection;
- packet transport hooks in the Android empty networking backend;
- story-mode client startup scaffolding and host/join console commands.

The branch still needs a successful Android build, two-device connection testing, and validation of player/entity replication in the actual game. Story progression, ALife simulation ownership, NPC/mutant synchronization, shared inventory, save/load, and reconnect behavior are not yet certified.

Do not use this branch as a replacement for the stable Android build or expect a complete co-op campaign yet.
