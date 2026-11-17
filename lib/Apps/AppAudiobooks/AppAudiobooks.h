#pragma once
#include "Config.h"

// Audiobooks (LILYGO audio build only): the player, the books on the card
// under /Audiobooks, Audiobookshelf (browse, stream, download) and where the
// sound goes (the USB-C audio module's Bluetooth or wired output, or USB
// headphones). Playback carries on after the app closes. The engine is
// lib/Book32_Audio; signing in to Audiobookshelf is on the web page.
#if defined(INKDECK_AUDIO) && INKDECK_AUDIO

#include <memory>
#include <string>
#include <vector>
#include "../../Book32_Core/BaseApp.h"
#include "../../Book32_Core/InputMgr.h"
#include "abs/AbsClient.h"
#include "abs/AbsQueue.h"
#include "audio/AudioChapter.h"
#include "system/AudioPlayer.h"

class AppAudiobooks : public App {
public:
    const char* getName() override { return "Audiobooks"; }
    const uint8_t* getIconImage() override;

    void start() override;
    void stop() override;
    void update() override;
    void draw() override;
    void forceRedraw() override;

private:
    enum class View : uint8_t { NowPlaying, Books, Chapters, Output, Libraries, Items, Detail, Downloads };
    enum class Act : uint8_t {
        None, Back, Show,
        PlayPause, Back30, Fwd30, PrevChapter, NextChapter, PrevTrack, NextTrack, VolDown, VolUp,
        OpenChapter, OpenBook, OpenRecent, PageUp, PageDown,
        UseBluetooth, UseWired, Scan, Connect, Forget, PowerModule, TestTone, SaveLog,
        OpenLibrary, OpenItem, OpenEpisode, Refresh,
        Stream, PlayDownloaded, Queue, QueueNow, RemoveDownload,
        StartNow, Pause, Remove, Up,
    };
    struct Target { int x, y, w, h; Act act; int index; };

    // Results of network jobs on AbsWorker; read and cleared on the main loop.
    struct Job {
        enum class Kind : uint8_t { Libraries, Items, Podcast, Detail } kind = Kind::Items;
        volatile bool done = false;
        bool ok = false;
        std::string error;
        std::vector<Abs::Library> libraries;
        std::vector<Abs::ItemSummary> items;
        int total = 0;
        Abs::Podcast podcast;
        Abs::ItemDetail detail;
        bool playWhenDone = false;
    };

    View _view = View::Books;
    std::vector<View> _history;
    std::vector<Target> _targets;
    bool _needsRedraw = true, _fullRefresh = true;
    unsigned long _lastDraw = 0;
    std::string _drawnKey;
    std::string _notice;

    std::vector<AudioPlayer::Book> _books;
    std::vector<AudioPlayer::RecentAudiobook> _recent;
    std::vector<AudioChapter> _chapters;
    int _first = 0;  // first row on the page of the current list
    bool _scanning = false;

    std::vector<Abs::Library> _libraries;
    Abs::Library _library;
    std::vector<Abs::ItemSummary> _items;
    int _itemPage = 0, _itemTotal = 0;
    Abs::Podcast _podcast;
    bool _showPodcast = false;       // the Detail view lists a podcast's episodes
    Abs::ItemSummary _item;          // the book the Detail view shows
    Abs::ItemDetail _detail;         // its tracks, once loaded
    std::shared_ptr<Job> _job;       // the network job in progress
    std::string _busy;               // what it is doing, for the screen
    uint32_t _queueSeen = 0;

    void handleInput(InputAction action);
    void handleTouch(uint16_t x, uint16_t y);
    void run(Act act, int index);
    void show(View view, bool remember = true);
    void back();
    void enterView();
    void finishJob();
    bool post(const char* what, Job::Kind kind, std::function<void(Job&)> work);
    void loadPodcast();
    void loadLibraries();
    void loadItems();
    void loadDetail(bool play);
    void streamRecent(const std::string& itemId);
    std::string stateKey();

    void render();
    void drawNowPlaying(int top);
    void drawBooks(int top);
    void drawChapters(int top);
    void drawOutput(int top);
    void drawLibraries(int top);
    void drawItems(int top);
    void drawDetail(int top);
    void drawDownloads(int top);
    int rowsFrom(int top) const;
    void button(int x, int y, int w, int h, const char* label, Act act, int index = 0, bool filled = false);
    void row(int y, const std::string& title, const std::string& detail, Act act, int index, bool marked = false);
    void pager(int y, int count, int rows);
    int text(const std::string& s, int x, int y, int size, int maxWidth);
    int paragraph(const std::string& s, int x, int y, int size, int maxWidth, int maxLines);
};

#endif
