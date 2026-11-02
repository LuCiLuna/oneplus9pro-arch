user_pref("media.hardware-video-decoding.enabled", true);
user_pref("media.hardware-video-decoding.force-enabled", true);
user_pref("media.ffmpeg.vaapi.enabled", true);
// SM8350 has no AV1 hardware; this makes YouTube pick VP9
user_pref("media.av1.enabled", false);
// Mobile layout, as Firefox for Android does: honor the site's viewport tag.
user_pref("dom.meta-viewport.enabled", true);
