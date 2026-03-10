# KickCraft — Handoff לצ'אט חדש

## הפרויקט
KickCraft — פלאג VST3+AU לעיבוד קיק, Gravitas Audio / Adi Perez.
JUCE 7, C++17. UI דרך WebBrowserComponent (WKWebView על Mac, WebView2 על Windows).

## מצב נוכחי
- ✅ Mac build עובד מושלם (VST3 + AU, Ableton Live)
- ❌ Windows build — UI יוצא לבן כי JUCE ברירת מחדל היא IE ישן

## המשימה
לבנות VST3 לWindows דרך **GitHub Actions** (לא build מקומי).

## מה ניסינו ונכשל
Build מקומי על Windows עם WebView2 SDK מ-NuGet — CMake לא הצליח למצוא
`WebView2LoaderStatic.lib` למרות שהקובץ קיים בנתיב הנכון. ניסינו:
- `target_link_directories` עם הנתיב
- נתיב מלא ישיר ב-`target_link_libraries`
- מחיקת CMakeCache ורצנו `cmake -B build_release` מחדש
- כלום לא עזר

## הפתרון: GitHub Actions
הקובץ `.github/workflows/build-windows.yml` כבר קיים בתיקייה.
הוא עושה:
1. Clone של JUCE 7.0.9
2. הורדת WebView2 SDK דרך NuGet
3. cmake + build
4. מעלה את ה-VST3 כ-artifact להורדה

## מבנה התיקייה
```
KICKCRAFT/
├── CMakeLists.txt          ← cross-platform (Mac + Windows)
├── Source/
│   ├── PluginProcessor.h
│   ├── PluginProcessor.cpp
│   ├── PluginEditor.h
│   └── PluginEditor.cpp
├── Resources/
│   └── ui.html
└── .github/
    └── workflows/
        └── build-windows.yml
```

## צעדים לצ'אט החדש
1. צור repo חדש ב-GitHub (private)
2. העלה את כל הקבצים מהתיקייה הזו
3. לך ל-Actions → "Build KickCraft VST3 (Windows)" → "Run workflow"
4. אחרי ~15 דקות — הורד את ה-artifact KickCraft-VST3-Windows
5. תן לAdi להתקין ולבדוק ב-Ableton

## פרמטרים
sub, trans, punch, body, click, air, tight, sat, clip, mix, out
כולם 0-1 חוץ מout שהוא -12 עד +12 dB.

## Bridge JS ↔ C++
URL interception דרך `pageAboutToLoad`:
- `juce://param?id=X&value=Y` — שינוי פרמטר
- `juce://chunk?n=X&total=Y&data=Z` — export WAV (base64 chunks)
- `juce://savekick?n=X&total=Y&data=Z` — שמירת קיק לrestore אחרי minimize
- `juce://export` — fallback export
