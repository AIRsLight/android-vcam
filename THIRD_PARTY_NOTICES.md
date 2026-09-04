# Third-party notices

The `vcam-streamer` executable contains the following statically linked
components:

- **FFmpeg 4.2.2**, built from tag `n4.2.2` with Mbed TLS and the FFmpeg
  `--enable-version3` option. This configuration is licensed under the GNU
  Lesser General Public License, version 3 or later. Source:
  <https://git.ffmpeg.org/ffmpeg.git>
- **Mbed TLS 3.6.7 LTS**, built from tag `mbedtls-3.6.7`, licensed under the
  Apache License 2.0. Source: <https://github.com/Mbed-TLS/mbedtls>

The corresponding source revisions, compatibility patch, and reproducible
Android build scripts are identified in `tools/build-ffmpeg-android.sh`,
`tools/build-mbedtls-android.sh`, and
`tools/patches/ffmpeg-4.2.2-mbedtls3.patch`. The android-vcam source containing
the `vcam-streamer` object code is available in this repository, allowing a
modified FFmpeg build to be relinked into the executable.

FFmpeg's license text is available at
<https://git.ffmpeg.org/gitweb/ffmpeg.git/blob/n4.2.2:/COPYING.LGPLv3> and Mbed
TLS's license text is available at
<https://github.com/Mbed-TLS/mbedtls/blob/mbedtls-3.6.7/LICENSE>.
