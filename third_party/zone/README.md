# Third-party source: Zone multiplayer packet definitions

This directory contains the packet data structures from
[AnarchadiaMC/Zone](https://github.com/AnarchadiaMC/Zone), specifically
`zone-client/src/protocol/packets.h`.

- Upstream project: Zone — S.T.A.L.K.E.R. Anomaly Multiplayer
- Upstream revision: current `main` snapshot inspected on 2026-11-21
- Copyright: Copyright (c) 2025 ZimZym
- License: MIT; see `LICENSE`

The packet definitions are retained as an interoperability/reference layer for
the Android LAN co-op implementation. Zone's Windows DLL injector, WinSock
client, Anomaly-specific Lua integration, and Go dedicated-server gameplay
implementation are not included here because they are coupled to Anomaly and
Windows-specific interfaces. The Android game integration remains native to
OpenXRay.
