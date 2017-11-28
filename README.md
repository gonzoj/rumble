__rumble__ is a bot for the voice chat software [Mumble](https://www.mumble.info/) that can be extended with Lua plugins.

Initially, I started working on this project to familiarize myself with [Protocol Buffers](https://developers.google.com/protocol-buffers/), [FFmpeg](https://www.ffmpeg.org/) and [Lua](https://www.lua.org/). It proved to be quite useful in the end, I still to this day run this thing on my Mumble server. Looking back, I would implement things quite a bit differently. Oh well.

Its features available through plugins include:

* Streaming from one channel to another channel (with delay)
* Playing YouTube videos (defunct)
* A matchmaking service that provides ladders, statistics etc.
* Playing configurable signature audio when users enter the server (incomplete)
* Exposing IP addresses to non-admin users (useful when hosting local gameservers)

I haven't bothered creating a proper build system, so it is probably quite the hassle to build this thing. Also, many things that should be configurable are not, so that's that. Maybe I'll clean this shit up one day. Probably not, though.

