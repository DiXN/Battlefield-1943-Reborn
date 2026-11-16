# Microsoft Edge WebView2 SDK (the parts the news window uses)

From the NuGet package Microsoft.Web.WebView2 1.0.3179.45 (nuget.org, BSD style licence: LICENSE.txt):

- `include/WebView2.h`: the COM interfaces (build/native/include).
- `x64/WebView2Loader.dll`: Microsoft's loader, signed by Microsoft Corporation (build/native/x64). It finds the
  WebView2 runtime that Windows 10 and 11 already have. The game loads it at run time from its own folder
  (bf/src/news_video.cpp); without it, or without the runtime, the news window shows a picture that opens the video
  in the browser instead.

Used only on Windows, by the news window's embedded YouTube player (docs/NEWS.md).
