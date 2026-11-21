# Android LAN transport smoke test

This host-side test exercises the IPv4/TCP framing helper independently of the game engine.

It verifies that:
- a listener accepts a local TCP connection;
- a length-prefixed frame arrives intact;
- the receiver can send the same frame back to the client.

Run it from the repository root on Linux:

```sh
g++ -std=c++17 -Wall -Wextra -Werror -pthread \
  src/xrNetServer/android/AndroidLanSocket.cpp \
  src/xrNetServer/android/tests/AndroidLanSocketTest.cpp \
  -o android-lan-socket-test
./android-lan-socket-test
```

Passing this test validates only the TCP transport helper. It does not mean the OpenXRay game client/server handshake or co-op gameplay is wired up yet.
