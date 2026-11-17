#include "AppAudiobooks.h"

#if defined(INKDECK_AUDIO) && INKDECK_AUDIO

#include <WiFi.h>
#include <algorithm>
#include "AppMgr.h"
#include "DisplayMgr.h"
#include "FontMgr.h"
#include "abs/AbsLocal.h"
#include "abs/AbsWifi.h"
#include "abs/AbsWorker.h"
#include "icon_audiobooks.h"
#include "system/AudioLink.h"
#include "system/AudioModule.h"
#include "system/UsbAudioOut.h"
#include "system/UsbAudioSink.h"

namespace {
constexpr int MARGIN = 18;
constexpr int HEADER_H = 64;
constexpr int ROW_H = 74;
constexpr int BUTTON_H = 64;
constexpr int PAGER_H = 64;
constexpr int ITEMS_PER_PAGE = 40;
constexpr unsigned long PLAYING_REDRAW_MS = 10000;

std::string clock(uint32_t ms) {
    const uint32_t s = ms / 1000;
    char out[16];
    if (s >= 3600) snprintf(out, sizeof(out), "%u:%02u:%02u", unsigned(s / 3600), unsigned(s / 60 % 60), unsigned(s % 60));
    else snprintf(out, sizeof(out), "%u:%02u", unsigned(s / 60), unsigned(s % 60));
    return out;
}

std::string hours(uint32_t seconds) {
    char out[16];
    if (seconds >= 3600) snprintf(out, sizeof(out), "%uh %02um", unsigned(seconds / 3600), unsigned(seconds / 60 % 60));
    else snprintf(out, sizeof(out), "%um", unsigned(seconds / 60));
    return out;
}

const char* queueState(AbsQueue::State s) {
    switch (s) {
        case AbsQueue::State::Waiting: return "Waiting for the charger";
        case AbsQueue::State::Starting: return "Starting";
        case AbsQueue::State::Downloading: return "Downloading";
        case AbsQueue::State::Failed: return "Failed";
        case AbsQueue::State::Paused: return "Paused";
    }
    return "";
}

const char* btText(audiolink::BtState s) {
    switch (s) {
        case audiolink::BtState::Off: return "off";
        case audiolink::BtState::Idle: return "not connected";
        case audiolink::BtState::Scanning: return "scanning";
        case audiolink::BtState::Connecting: return "connecting";
        case audiolink::BtState::Connected: return "connected";
    }
    return "";
}
}  // namespace

const uint8_t* AppAudiobooks::getIconImage() { return icon_audiobooks_160x160; }

void AppAudiobooks::start() {
    _history.clear();
    _notice.clear();
    _queueSeen = AbsQueue::changeCounter();
    show(AudioPlayer::view().open ? View::NowPlaying : View::Books, false);
    InputMgr::getInstance().setCallback([this](InputAction a) { handleInput(a); });
    InputMgr::getInstance().setTouchCallback([this](uint16_t x, uint16_t y) { handleTouch(x, y); });
}

void AppAudiobooks::stop() {
    if (_scanning) AudioPlayer::btScan(false);
    _scanning = false;
    _job.reset();  // a job still running finishes into its own copy
    InputMgr::getInstance().clearCallback();
    InputMgr::getInstance().clearTouchCallback();
}

void AppAudiobooks::forceRedraw() {
    _fullRefresh = true;
    _needsRedraw = true;
}

void AppAudiobooks::handleInput(InputAction action) {
    if (action == INPUT_BACK || action == INPUT_HOME) {
        if (action == INPUT_HOME) _history.clear();
        back();
    } else if (action == INPUT_SELECT && _view == View::NowPlaying) {
        run(Act::PlayPause, 0);
    } else if (action == INPUT_NEXT) {
        run(_view == View::NowPlaying ? Act::VolUp : Act::PageDown, rowsFrom(HEADER_H + 80));
    } else if (action == INPUT_PREV) {
        run(_view == View::NowPlaying ? Act::VolDown : Act::PageUp, rowsFrom(HEADER_H + 80));
    }
}

void AppAudiobooks::handleTouch(uint16_t x, uint16_t y) {
    for (const Target& t : _targets) {
        if (x >= t.x && x < t.x + t.w && y >= t.y && y < t.y + t.h) {
            run(t.act, t.index);
            return;
        }
    }
}

void AppAudiobooks::show(View view, bool remember) {
    if (remember && view != _view) _history.push_back(_view);
    if (_view == View::Output && view != View::Output && _scanning) {
        AudioPlayer::btScan(false);
        _scanning = false;
    }
    _view = view;
    _first = 0;
    _notice.clear();
    enterView();
    _fullRefresh = true;
    _needsRedraw = true;
}

void AppAudiobooks::back() {
    if (_job && !_job->done) _job.reset();  // leave it; its result is dropped
    _busy.clear();
    if (_history.empty()) {
        AppMgr::getInstance().switchTo(0);
        return;
    }
    View previous = _history.back();
    _history.pop_back();
    show(previous, false);
}

// Data a view shows, loaded as it opens (SD reads on the main loop; network
// on AbsWorker).
void AppAudiobooks::enterView() {
    switch (_view) {
        case View::Books:
            _books = AudioPlayer::listBooks();
            _recent = AudioPlayer::recentAudiobooks();
            break;
        case View::Chapters: {
            _chapters = AudioPlayer::chapters();
            const int current = AudioPlayer::view().chapter;
            const int rows = std::max(1, rowsFrom(HEADER_H + 10));
            if (current > 0) _first = current / rows * rows;
            break;
        }
        case View::Output:
            AudioPlayer::wakeModule();  // power and link on, so the module or headphones appear
            break;
        case View::Libraries:
            loadLibraries();
            break;
        case View::Items:
            loadItems();
            break;
        case View::Detail:
            if (_showPodcast) loadPodcast();
            else loadDetail(false);
            break;
        default:
            break;
    }
}

bool AppAudiobooks::post(const char* what, Job::Kind kind, std::function<void(Job&)> work) {
    auto job = std::make_shared<Job>();
    job->kind = kind;
    _job = job;
    _busy = what;
    _needsRedraw = true;
    const bool posted = AbsWorker::post([job, work] {
        std::string failure;
        AbsWifi::Use wifi(failure);
        if (!wifi.ok()) {
            job->error = failure.empty() ? "Wi-Fi is not connected" : failure;
        } else {
            work(*job);
        }
        job->done = true;
    });
    if (!posted) {
        _job.reset();
        _busy.clear();
        _notice = "Busy; try again in a moment";
    }
    return posted;
}

void AppAudiobooks::loadLibraries() {
    if (!Abs::signedIn()) return;
    post("Loading libraries", Job::Kind::Libraries, [](Job& job) {
        job.ok = Abs::libraries(job.libraries, job.error);
    });
}

void AppAudiobooks::loadItems() {
    _items.clear();
    const std::string id = _library.id;
    const int page = _itemPage;
    // A podcast library lists its shows here; a show's Detail lists episodes.
    post(_library.podcasts ? "Loading shows" : "Loading books", Job::Kind::Items, [id, page](Job& job) {
        job.ok = Abs::items(id, page, ITEMS_PER_PAGE, job.items, job.total, job.error);
    });
}

void AppAudiobooks::loadPodcast() {
    _podcast = Abs::Podcast{};
    const std::string id = _item.id;
    post("Loading episodes", Job::Kind::Podcast, [id](Job& job) { job.ok = Abs::podcast(id, 30, job.podcast, job.error); });
}

void AppAudiobooks::loadDetail(bool play) {
    _detail = Abs::ItemDetail{};
    AbsLocal::BookMeta meta;
    const bool offline = !AbsWifi::connected() && WiFi.status() != WL_CONNECTED;
    if (offline && AbsLocal::find(_item.id, meta) && meta.complete) {
        _detail.id = meta.itemId;
        _detail.title = meta.title;
        _detail.author = meta.author;
        _detail.durationMs = meta.durationMs;
        _detail.tracks = meta.tracks;
        _detail.chapters = meta.chapters;
        return;
    }
    const std::string id = _item.id;
    if (post(play ? "Opening" : "Loading", Job::Kind::Detail, [id](Job& job) { job.ok = Abs::item(id, job.detail, job.error); }))
        _job->playWhenDone = play;
}

void AppAudiobooks::streamRecent(const std::string& itemId) {
    _item = Abs::ItemSummary{};
    _item.id = itemId;
    if (post("Opening", Job::Kind::Detail, [itemId](Job& job) { job.ok = Abs::item(itemId, job.detail, job.error); }))
        _job->playWhenDone = true;
}

// A finished job's results, on the main loop.
void AppAudiobooks::finishJob() {
    std::shared_ptr<Job> job = _job;
    _job.reset();
    _busy.clear();
    _needsRedraw = true;
    if (!job->ok) {
        _notice = job->error.empty() ? "Could not reach Audiobookshelf" : job->error;
        return;
    }
    switch (job->kind) {
        case Job::Kind::Libraries:
            _libraries = job->libraries;
            // A single library opens straight away.
            if (_libraries.size() == 1 && _view == View::Libraries) {
                _library = _libraries[0];
                _itemPage = 0;
                show(View::Items);
            }
            break;
        case Job::Kind::Detail:
            _detail = job->detail;
            if (job->playWhenDone) {
                if (AudioPlayer::openRemote(_detail)) show(View::NowPlaying);
                else _notice = AudioPlayer::view().message;
            }
            break;
        case Job::Kind::Podcast:
            _podcast = job->podcast;
            break;
        case Job::Kind::Items:
            _items = job->items;
            _itemTotal = job->total;
            break;
    }
}

void AppAudiobooks::run(Act act, int index) {
    _needsRedraw = true;
    const AudioPlayer::View v = AudioPlayer::view();
    switch (act) {
        case Act::None: break;
        case Act::Back: back(); break;
        case Act::Show: show(static_cast<View>(index)); break;
        case Act::Up: back(); break;

        case Act::PlayPause: AudioPlayer::togglePause(); break;
        case Act::Back30: AudioPlayer::skip(-30000); break;
        case Act::Fwd30: AudioPlayer::skip(30000); break;
        case Act::PrevChapter: v.chapterCount > 0 ? AudioPlayer::previousChapter() : AudioPlayer::previousTrack(); break;
        case Act::NextChapter: v.chapterCount > 0 ? AudioPlayer::nextChapter() : AudioPlayer::nextTrack(); break;
        case Act::PrevTrack: AudioPlayer::previousTrack(); break;
        case Act::NextTrack: AudioPlayer::nextTrack(); break;
        case Act::VolDown: AudioPlayer::setVolume(v.volume >= 5 ? v.volume - 5 : 0); break;
        case Act::VolUp: AudioPlayer::setVolume(std::min(100, v.volume + 5)); break;
        case Act::OpenChapter:
            AudioPlayer::seekChapter(index);
            back();
            break;

        case Act::OpenBook:
            if (index >= 0 && index < int(_books.size())) {
                if (AudioPlayer::open(_books[index])) show(View::NowPlaying);
                else _notice = AudioPlayer::view().message;
            }
            break;
        case Act::OpenRecent:
            if (index >= 0 && index < int(_recent.size())) {
                const auto& r = _recent[index];
                if (AudioPlayer::openRecent(r)) show(View::NowPlaying);
                else if (!r.itemId.empty() && Abs::signedIn()) streamRecent(r.itemId);
                else _notice = "This book is no longer on the card";
            }
            break;
        case Act::PageUp:
            _first = std::max(0, _first - std::max(1, index));
            if (_view == View::Items && !_library.podcasts && _first == 0 && _itemPage > 0) {
                --_itemPage;
                loadItems();
            }
            break;
        case Act::PageDown:
            _first += std::max(1, index);
            if (_view == View::Items && !_library.podcasts && _first >= int(_items.size()) &&
                (_itemPage + 1) * ITEMS_PER_PAGE < _itemTotal) {
                ++_itemPage;
                _first = 0;
                loadItems();
            }
            break;

        case Act::UseBluetooth: AudioPlayer::selectOutput(audiolink::Output::Bluetooth); break;
        case Act::UseWired: AudioPlayer::selectOutput(audiolink::Output::I2s); break;
        case Act::Scan:
            _scanning = !_scanning;
            if (_scanning) AudioLink::clearBtDevices();
            AudioPlayer::btScan(_scanning);
            break;
        case Act::Connect: {
            const auto devices = AudioLink::btDevices();
            if (index >= 0 && index < int(devices.size())) {
                if (_scanning) AudioPlayer::btScan(false);
                _scanning = false;
                AudioPlayer::btConnect(devices[index].address);
            }
            break;
        }
        case Act::Forget: AudioPlayer::btForget(); break;
        case Act::PowerModule: {
            const bool on = !AudioModule::isLinkEnabled();
            if (on) AudioPlayer::wakeModule();
            else {
                AudioModule::setLinkEnabled(false);
                AudioModule::setPower(false);
            }
            break;
        }
        case Act::TestTone:
            if (!UsbAudioOut::playTestTone()) _notice = "Plug in USB headphones (and stop the book) first";
            break;
        case Act::SaveLog:
            _notice = AudioModule::saveLog() ? "Saved to /audio-log.txt on the card" : "Could not write the card";
            break;

        case Act::OpenLibrary:
            if (index >= 0 && index < int(_libraries.size())) {
                _library = _libraries[index];
                _itemPage = 0;
                show(View::Items);
            }
            break;
        case Act::OpenItem:
            if (index >= 0 && index < int(_items.size())) {
                _item = _items[index];
                _showPodcast = _library.podcasts;  // a show: Detail lists its newest episodes
                show(View::Detail);
            }
            break;
        case Act::OpenEpisode:
            if (index >= 0 && index < int(_podcast.episodes.size())) {
                _showPodcast = false;
                _item = Abs::ItemSummary{};
                _item.id = _podcast.id + "/" + _podcast.episodes[index].id;
                _item.title = _podcast.episodes[index].title;
                _item.author = _podcast.title;
                loadDetail(true);
            }
            break;
        case Act::Refresh:
            enterView();
            break;

        case Act::Stream:
            if (!_detail.id.empty()) {
                if (AudioPlayer::openRemote(_detail)) show(View::NowPlaying);
                else _notice = AudioPlayer::view().message;
            } else {
                loadDetail(true);
            }
            break;
        case Act::PlayDownloaded: {
            AbsLocal::BookMeta meta;
            if (AbsLocal::find(_item.id, meta) && meta.complete) {
                AudioPlayer::Book book{meta.folder, meta.title, true, meta.itemId};
                if (AudioPlayer::open(book)) show(View::NowPlaying);
                else _notice = AudioPlayer::view().message;
            }
            break;
        }
        case Act::Queue:
        case Act::QueueNow:
            AbsQueue::add(_item.id, _item.title.empty() ? _detail.title : _item.title,
                          _item.author.empty() ? _detail.author : _item.author, act == Act::QueueNow);
            if (act == Act::QueueNow) AbsQueue::startNow(_item.id);
            _notice = act == Act::QueueNow ? "Downloading now" : "Queued: downloads while charging";
            break;
        case Act::RemoveDownload: {
            if (AbsQueue::contains(_item.id)) AbsQueue::remove(_item.id);
            AbsLocal::BookMeta meta;
            if (AbsLocal::find(_item.id, meta)) AbsLocal::remove(meta);
            _notice = "Download removed";
            break;
        }

        case Act::StartNow:
        case Act::Pause:
        case Act::Remove: {
            const auto entries = AbsQueue::entries();
            if (index < 0 || index >= int(entries.size())) break;
            const std::string& id = entries[index].itemId;
            if (act == Act::StartNow) AbsQueue::startNow(id);
            else if (act == Act::Pause) AbsQueue::pause(id);
            else AbsQueue::remove(id);
            break;
        }
    }
}

// What the screen shows that changes on its own, so update() can tell when
// to redraw. Position only to the 10 s while playing (a refresh each time).
std::string AppAudiobooks::stateKey() {
    const AudioPlayer::View v = AudioPlayer::view();
    std::string key = std::to_string(int(_view)) + v.title + "|" + std::to_string(v.paused) +
                      std::to_string(v.waitingForModule) + std::to_string(v.finished) + "|" +
                      std::to_string(v.chapter) + "|" + std::to_string(v.track) + "|" + std::to_string(v.volume) +
                      "|" + std::to_string(int(v.btState)) + std::to_string(int(v.output)) + v.message;
    if (_view == View::NowPlaying && !v.paused) key += "|" + std::to_string(v.bookPositionMs / PLAYING_REDRAW_MS);
    if (_view == View::Output) {
        const auto snap = AudioModule::snapshot();
        key += "|" + std::to_string(snap.powerOn) + std::to_string(snap.linkOn) + std::to_string(int(snap.linkState)) +
               std::to_string(int(snap.headphonesState)) + std::to_string(AudioLink::btDevices().size()) +
               std::to_string(AudioLink::ready());
    }
    if (_view == View::Downloads || _view == View::Detail) key += "|" + std::to_string(AbsQueue::changeCounter());
    return key;
}

void AppAudiobooks::update() {
    if (_job && _job->done) finishJob();
    if (_needsRedraw) return;
    const unsigned long now = millis();
    if (now - _lastDraw < 1000) return;  // at most one refresh a second
    if (stateKey() != _drawnKey) _needsRedraw = true;
}

void AppAudiobooks::draw() {
    if (!_needsRedraw) return;
    _needsRedraw = false;
    render();
}

// ---------------------------------------------------------------------------
// Drawing

int AppAudiobooks::rowsFrom(int top) const {
    const int h = DisplayMgr::getInstance().getDisplay().height();
    return std::max(1, (h - top - PAGER_H - 40) / ROW_H);
}

int AppAudiobooks::text(const std::string& s, int x, int y, int size, int maxWidth) {
    FontMgr& f = FontMgr::getInstance();
    std::string t = s;
    if (f.getTextWidth(t.c_str(), size) > maxWidth) {
        while (!t.empty() && f.getTextWidth((t + "...").c_str(), size) > maxWidth) t.pop_back();
        t += "...";
    }
    f.drawText(DisplayMgr::getInstance().getDisplay(), t.c_str(), x, y, size, GxEPD_BLACK);
    return f.getTextWidth(t.c_str(), size);
}

int AppAudiobooks::paragraph(const std::string& s, int x, int y, int size, int maxWidth, int maxLines) {
    FontMgr& f = FontMgr::getInstance();
    const int lineH = f.getTextHeight(size) + 8;
    std::string line, word;
    int lines = 0;
    auto flush = [&](bool last) {
        if (lines + 1 >= maxLines && !last) {
            text(line + " " + word, x, y, size, maxWidth);  // truncated with "..."
            line.clear();
            ++lines;
            return false;
        }
        f.drawText(DisplayMgr::getInstance().getDisplay(), line.c_str(), x, y, size, GxEPD_BLACK);
        y += lineH;
        ++lines;
        line.clear();
        return true;
    };
    for (size_t i = 0; i <= s.size() && lines < maxLines; ++i) {
        const char c = i < s.size() ? s[i] : ' ';
        if (c != ' ' && c != '\n') { word += c; continue; }
        const std::string candidate = line.empty() ? word : line + " " + word;
        if (!line.empty() && f.getTextWidth(candidate.c_str(), size) > maxWidth) {
            if (!flush(false)) return y;
            line = word;
        } else {
            line = candidate;
        }
        word.clear();
        if (c == '\n' && !line.empty() && !flush(false)) return y;
    }
    if (!line.empty() && lines < maxLines) flush(true);
    return y;
}

void AppAudiobooks::button(int x, int y, int w, int h, const char* label, Act act, int index, bool filled) {
    Book32Display& d = DisplayMgr::getInstance().getDisplay();
    FontMgr& f = FontMgr::getInstance();
    if (filled) d.fillRoundRect(x, y, w, h, 10, GxEPD_BLACK);
    else {
        d.drawRoundRect(x, y, w, h, 10, GxEPD_BLACK);
        d.drawRoundRect(x + 1, y + 1, w - 2, h - 2, 9, GxEPD_BLACK);
    }
    const int tw = f.getTextWidth(label, FONT_SIZE_MENU);
    f.drawText(d, label, x + (w - tw) / 2, y + h / 2 + 8, FONT_SIZE_MENU, filled ? GxEPD_WHITE : GxEPD_BLACK);
    _targets.push_back({x, y, w, h, act, index});
}

void AppAudiobooks::row(int y, const std::string& title, const std::string& detail, Act act, int index, bool marked) {
    Book32Display& d = DisplayMgr::getInstance().getDisplay();
    const int w = d.width();
    const int textX = MARGIN + (marked ? 26 : 0);
    if (marked) d.fillTriangle(MARGIN, y + 18, MARGIN, y + 40, MARGIN + 16, y + 29, GxEPD_BLACK);
    text(title, textX, y + 30, FONT_SIZE_MENU, w - textX - MARGIN);
    if (!detail.empty()) text(detail, textX, y + 58, FONT_SIZE_SMALL, w - textX - MARGIN);
    d.drawLine(MARGIN, y + ROW_H - 2, w - MARGIN, y + ROW_H - 2, GxEPD_BLACK);
    _targets.push_back({0, y, w, ROW_H, act, index});
}

// Previous / next page buttons under a list of `count` rows starting at _first.
void AppAudiobooks::pager(int y, int count, int rows) {
    Book32Display& d = DisplayMgr::getInstance().getDisplay();
    const int w = (d.width() - 3 * MARGIN) / 2;
    const bool more = _first + rows < count ||
                      (_view == View::Items && !_library.podcasts && (_itemPage + 1) * ITEMS_PER_PAGE < _itemTotal);
    const bool less = _first > 0 || (_view == View::Items && _itemPage > 0);
    if (less) button(MARGIN, y, w, PAGER_H - 8, "< Previous", Act::PageUp, rows);
    if (more) button(2 * MARGIN + w, y, w, PAGER_H - 8, "Next >", Act::PageDown, rows);
}

void AppAudiobooks::render() {
    DisplayMgr& mgr = DisplayMgr::getInstance();
    Book32Display& d = mgr.getDisplay();
    FontMgr& f = FontMgr::getInstance();
    _targets.clear();

    if (_fullRefresh) d.setFullWindow();
    else d.setPartialWindow(0, 0, d.width(), d.height());
    _fullRefresh = false;

    static const char* titles[] = {"Now playing", "Audiobooks", "Chapters", "Sound output",
                                   "Audiobookshelf", "", "", "Downloads"};
    std::string title = titles[int(_view)];
    if (_view == View::Items) title = _library.name;
    if (_view == View::Detail) title = _showPodcast ? "Podcast" : "Book";

    d.firstPage();
    do {
        d.fillScreen(GxEPD_WHITE);
        // Header: back arrow, title, the Books shortcut while playing.
        f.drawText(d, "<", MARGIN, 44, FONT_SIZE_TITLE, GxEPD_BLACK);
        _targets.push_back({0, 0, 80, HEADER_H, Act::Back, 0});
        text(title, 64, 44, FONT_SIZE_SUBTITLE, d.width() - 64 - 150);
        if (_view == View::NowPlaying) button(d.width() - 140, 8, 124, 48, "Books", Act::Show, int(View::Books));
        else if (AudioPlayer::view().open && _view != View::Chapters)
            button(d.width() - 140, 8, 124, 48, "Playing", Act::Show, int(View::NowPlaying));
        d.drawLine(0, HEADER_H, d.width(), HEADER_H, GxEPD_BLACK);

        const int top = HEADER_H + 10;
        switch (_view) {
            case View::NowPlaying: drawNowPlaying(top); break;
            case View::Books: drawBooks(top); break;
            case View::Chapters: drawChapters(top); break;
            case View::Output: drawOutput(top); break;
            case View::Libraries: drawLibraries(top); break;
            case View::Items: drawItems(top); break;
            case View::Detail: drawDetail(top); break;
            case View::Downloads: drawDownloads(top); break;
        }

        // Status line: work in progress, or the last notice.
        const std::string status = !_busy.empty() ? _busy + "..." : _notice;
        if (!status.empty()) {
            d.fillRect(0, d.height() - 40, d.width(), 40, GxEPD_WHITE);
            d.drawLine(0, d.height() - 40, d.width(), d.height() - 40, GxEPD_BLACK);
            text(status, MARGIN, d.height() - 12, FONT_SIZE_SMALL, d.width() - 2 * MARGIN);
        }
    } while (d.nextPage());

    _lastDraw = millis();
    _drawnKey = stateKey();
}

void AppAudiobooks::drawNowPlaying(int top) {
    Book32Display& d = DisplayMgr::getInstance().getDisplay();
    const int w = d.width();
    const AudioPlayer::View v = AudioPlayer::view();
    int y = top + 30;
    if (!v.open) {
        paragraph("Nothing is playing. Choose a book from your card or from Audiobookshelf.", MARGIN, y,
                  FONT_SIZE_BODY, w - 2 * MARGIN, 4);
        button(MARGIN, y + 120, w - 2 * MARGIN, BUTTON_H, "Choose a book", Act::Show, int(View::Books), true);
        return;
    }
    y = paragraph(v.title, MARGIN, y, FONT_SIZE_SUBTITLE, w - 2 * MARGIN, 3) + 4;
    if (v.chapter >= 0) {
        text(v.chapterTitle.empty() ? "Chapter " + std::to_string(v.chapter + 1) : v.chapterTitle, MARGIN, y,
             FONT_SIZE_BODY, w - 2 * MARGIN);
        y += 30;
        text("Chapter " + std::to_string(v.chapter + 1) + " of " + std::to_string(v.chapterCount), MARGIN, y,
             FONT_SIZE_SMALL, w - 2 * MARGIN);
    } else if (v.trackCount > 1) {
        text(v.trackName, MARGIN, y, FONT_SIZE_BODY, w - 2 * MARGIN);
        y += 30;
        text("Track " + std::to_string(v.track + 1) + " of " + std::to_string(v.trackCount), MARGIN, y,
             FONT_SIZE_SMALL, w - 2 * MARGIN);
    }
    y += 40;

    // Progress through the chapter (or track), then the whole book.
    const uint32_t pos = v.chapter >= 0 ? v.chapterPositionMs : v.positionMs;
    const uint32_t len = v.chapter >= 0 ? v.chapterDurationMs : v.durationMs;
    d.drawRect(MARGIN, y, w - 2 * MARGIN, 18, GxEPD_BLACK);
    if (len > 0) d.fillRect(MARGIN, y, int64_t(w - 2 * MARGIN) * std::min(pos, len) / len, 18, GxEPD_BLACK);
    y += 44;
    FontMgr& f = FontMgr::getInstance();
    f.drawText(d, clock(pos).c_str(), MARGIN, y, FONT_SIZE_SMALL, GxEPD_BLACK);
    if (len > 0) f.drawTextRight(d, ("-" + clock(len - std::min(pos, len))).c_str(), w - MARGIN, y, FONT_SIZE_SMALL);
    y += 28;
    if (v.bookDurationMs > 0) {
        const int pct = int(uint64_t(v.bookPositionMs) * 100 / v.bookDurationMs);
        text("Book: " + std::to_string(pct) + "%, " + hours((v.bookDurationMs - std::min(v.bookPositionMs, v.bookDurationMs)) / 1000) + " left",
             MARGIN, y, FONT_SIZE_SMALL, w - 2 * MARGIN);
    }
    y += 30;

    // Transport.
    const int gap = 12;
    const int bw = (w - 2 * MARGIN - 4 * gap) / 5;
    const int by = y;
    button(MARGIN + 0 * (bw + gap), by, bw, 90, "|<", Act::PrevChapter);
    button(MARGIN + 1 * (bw + gap), by, bw, 90, "-30", Act::Back30);
    button(MARGIN + 2 * (bw + gap), by, bw, 90, v.paused || v.finished ? "Play" : "Pause", Act::PlayPause, 0, true);
    button(MARGIN + 3 * (bw + gap), by, bw, 90, "+30", Act::Fwd30);
    button(MARGIN + 4 * (bw + gap), by, bw, 90, ">|", Act::NextChapter);
    y = by + 90 + 24;

    // Volume.
    const int vb = 90;
    button(MARGIN, y, vb, BUTTON_H, "-", Act::VolDown);
    button(w - MARGIN - vb, y, vb, BUTTON_H, "+", Act::VolUp);
    const int barX = MARGIN + vb + 16, barW = w - 2 * (MARGIN + vb + 16);
    d.drawRect(barX, y + BUTTON_H / 2 - 9, barW, 18, GxEPD_BLACK);
    d.fillRect(barX, y + BUTTON_H / 2 - 9, barW * v.volume / 100, 18, GxEPD_BLACK);
    y += BUTTON_H + 24;

    // Where it plays.
    std::string out;
    if (v.waitingForModule) out = "Waiting for the audio module or USB headphones";
    else if (v.output == UsbAudioSink::USB_HEADPHONES) out = "USB headphones: " + std::string(UsbAudioOut::device().product);
    else if (v.output == audiolink::Output::Bluetooth) out = std::string("Bluetooth: ") + btText(v.btState);
    else if (v.output == audiolink::Output::I2s) out = "Audio module: wired output";
    else out = "Audio module: test output";
    text(out, MARGIN, y + 6, FONT_SIZE_SMALL, w - 2 * MARGIN);
    if (v.remote) text("Streaming from Audiobookshelf", MARGIN, y + 32, FONT_SIZE_SMALL, w - 2 * MARGIN);
    y += 56;

    const int half = (w - 3 * MARGIN) / 2;
    if (v.chapterCount > 0) button(MARGIN, y, half, BUTTON_H, "Chapters", Act::Show, int(View::Chapters));
    button(v.chapterCount > 0 ? 2 * MARGIN + half : MARGIN, y, v.chapterCount > 0 ? half : w - 2 * MARGIN, BUTTON_H,
           "Sound output", Act::Show, int(View::Output));
    if (!v.message.empty() && _notice.empty()) _notice = v.message;
}

void AppAudiobooks::drawBooks(int top) {
    Book32Display& d = DisplayMgr::getInstance().getDisplay();
    const int w = d.width();
    const int third = (w - 4 * MARGIN) / 3;
    button(MARGIN, top, third, BUTTON_H, "Server", Act::Show, int(View::Libraries));
    button(2 * MARGIN + third, top, third, BUTTON_H, "Downloads", Act::Show, int(View::Downloads));
    button(3 * MARGIN + 2 * third, top, third, BUTTON_H, "Output", Act::Show, int(View::Output));
    int y = top + BUTTON_H + 14;

    // Recent first (they resume), then the rest of the card.
    struct Line { std::string title, detail; Act act; int index; };
    std::vector<Line> lines;
    for (size_t i = 0; i < _recent.size(); ++i) {
        const auto& r = _recent[i];
        std::string detail = r.author;
        if (r.durationMs > 0) {
            detail += (detail.empty() ? "" : ", ") + std::to_string(int(uint64_t(r.positionMs) * 100 / r.durationMs)) + "%";
        }
        detail += r.path.empty() ? " (stream)" : "";
        lines.push_back({r.title, "Recent: " + detail, Act::OpenRecent, int(i)});
    }
    for (size_t i = 0; i < _books.size(); ++i) {
        bool recent = false;
        for (const auto& r : _recent) recent = recent || (!r.path.empty() && r.path == _books[i].path);
        if (!recent) lines.push_back({_books[i].title, _books[i].folder ? "Folder" : "File", Act::OpenBook, int(i)});
    }
    if (lines.empty()) {
        paragraph(std::string("No audiobooks yet. Copy .m4b, .m4a or .mp3 files (or a folder per book) to ") +
                      AudioPlayer::BOOKS_DIRECTORY + " on the MicroSD card, or open the Server.",
                  MARGIN, y + 30, FONT_SIZE_BODY, w - 2 * MARGIN, 6);
        return;
    }
    const int rows = rowsFrom(y);
    if (_first >= int(lines.size())) _first = 0;
    for (int i = _first; i < int(lines.size()) && i < _first + rows; ++i, y += ROW_H)
        row(y, lines[i].title, lines[i].detail, lines[i].act, lines[i].index);
    pager(d.height() - PAGER_H - 44, int(lines.size()), rows);
}

void AppAudiobooks::drawChapters(int top) {
    const int current = AudioPlayer::view().chapter;
    const int rows = rowsFrom(top);
    int y = top;
    for (int i = _first; i < int(_chapters.size()) && i < _first + rows; ++i, y += ROW_H) {
        const auto& c = _chapters[i];
        row(y, c.title.empty() ? "Chapter " + std::to_string(i + 1) : c.title, clock(c.startMs), Act::OpenChapter, i,
            i == current);
    }
    pager(DisplayMgr::getInstance().getDisplay().height() - PAGER_H - 44, int(_chapters.size()), rows);
}

void AppAudiobooks::drawOutput(int top) {
    Book32Display& d = DisplayMgr::getInstance().getDisplay();
    const int w = d.width();
    const auto snap = AudioModule::snapshot();
    const AudioPlayer::View v = AudioPlayer::view();
    int y = top + 24;

    // USB-C port: module, headphones, or nothing yet.
    std::string port = "USB-C port: ";
    if (!snap.linkOn) port += "off";
    else if (snap.headphonesState == UsbAudioOut::State::Ready || snap.headphonesState == UsbAudioOut::State::Playing)
        port += std::string("headphones (") + snap.headphones.product + ")";
    else if (AudioLink::ready()) port += "audio module";
    else port += "waiting for a device";
    text(port, MARGIN, y, FONT_SIZE_BODY, w - 2 * MARGIN);
    y += 30;
    if (snap.externalVbus) text("Powered from outside (charger or PC)", MARGIN, y, FONT_SIZE_SMALL, w - 2 * MARGIN);
    else text(std::string("Power to the port: ") + (snap.powerOn ? "on" : "off"), MARGIN, y, FONT_SIZE_SMALL, w - 2 * MARGIN);
    y += 20;
    const int half = (w - 3 * MARGIN) / 2;
    button(MARGIN, y, half, BUTTON_H, snap.linkOn ? "Port off" : "Port on", Act::PowerModule);
    button(2 * MARGIN + half, y, half, BUTTON_H, "Save log", Act::SaveLog);
    y += BUTTON_H + 26;

    const bool headphones = snap.headphonesState == UsbAudioOut::State::Ready || snap.headphonesState == UsbAudioOut::State::Playing;
    if (headphones) {
        paragraph(std::string("Books play on the USB headphones. ") + snap.headphones.formats, MARGIN, y, FONT_SIZE_SMALL,
                  w - 2 * MARGIN, 2);
        y += 56;
        button(MARGIN, y, w - 2 * MARGIN, BUTTON_H, "Test tone", Act::TestTone);
        return;
    }
    if (!AudioLink::ready()) {
        paragraph("Plug the audio module or a USB headphone adapter into the USB-C port. While the port is on, "
                  "the USB console is off; to flash, hold BOOT and tap RST.",
                  MARGIN, y, FONT_SIZE_SMALL, w - 2 * MARGIN, 4);
        return;
    }

    // The module's outputs and its Bluetooth.
    button(MARGIN, y, half, BUTTON_H, "Bluetooth", Act::UseBluetooth, 0, v.output == audiolink::Output::Bluetooth);
    button(2 * MARGIN + half, y, half, BUTTON_H, "Wired", Act::UseWired, 0, v.output == audiolink::Output::I2s);
    y += BUTTON_H + 30;
    if (v.output != audiolink::Output::Bluetooth) return;
    text(std::string("Bluetooth: ") + btText(v.btState), MARGIN, y, FONT_SIZE_BODY, w - 2 * MARGIN);
    y += 20;
    button(MARGIN, y, half, BUTTON_H, _scanning ? "Stop search" : "Search", Act::Scan, 0, _scanning);
    button(2 * MARGIN + half, y, half, BUTTON_H, "Forget", Act::Forget);
    y += BUTTON_H + 12;
    const auto devices = AudioLink::btDevices();
    if (devices.empty() && _scanning) text("Put the headphones in pairing mode", MARGIN, y + 30, FONT_SIZE_SMALL, w - 2 * MARGIN);
    const int rows = rowsFrom(y);
    for (int i = 0; i < int(devices.size()) && i < rows; ++i, y += ROW_H)
        row(y, devices[i].name.empty() ? "(no name)" : devices[i].name, std::to_string(devices[i].rssi) + " dBm",
            Act::Connect, i);
}

void AppAudiobooks::drawLibraries(int top) {
    const int w = DisplayMgr::getInstance().getDisplay().width();
    if (!Abs::signedIn()) {
        std::string where = WiFi.status() == WL_CONNECTED ? "http://" + std::string(WiFi.localIP().toString().c_str()) + "/"
                                                          : "the device's web page";
        paragraph("Sign in to your Audiobookshelf server on " + where + " (Audiobooks card), then come back here.",
                  MARGIN, top + 40, FONT_SIZE_BODY, w - 2 * MARGIN, 6);
        button(MARGIN, top + 240, w - 2 * MARGIN, BUTTON_H, "Check again", Act::Refresh);
        return;
    }
    text(Abs::username() + " on " + Abs::serverUrl(), MARGIN, top + 26, FONT_SIZE_SMALL, w - 2 * MARGIN);
    int y = top + 44;
    for (int i = 0; i < int(_libraries.size()); ++i, y += ROW_H)
        row(y, _libraries[i].name, _libraries[i].podcasts ? "Podcasts" : "Books", Act::OpenLibrary, i);
    if (_libraries.empty() && _busy.empty()) button(MARGIN, y + 20, w - 2 * MARGIN, BUTTON_H, "Try again", Act::Refresh);
}

void AppAudiobooks::drawItems(int top) {
    const int rows = rowsFrom(top);
    int y = top;
    if (_first >= int(_items.size())) _first = 0;
    for (int i = _first; i < int(_items.size()) && i < _first + rows; ++i, y += ROW_H) {
        const auto& it = _items[i];
        std::string detail = it.author;
        if (it.durationSec) detail += (detail.empty() ? "" : ", ") + hours(it.durationSec);
        if (it.finished) detail += ", finished";
        else if (it.progress > 0.005f) detail += ", " + std::to_string(int(it.progress * 100)) + "%";
        if (AbsQueue::contains(it.id)) detail += ", queued";
        row(y, it.title, detail, Act::OpenItem, i);
    }
    if (_itemTotal > ITEMS_PER_PAGE) {
        text(std::to_string(_itemPage * ITEMS_PER_PAGE + _first + 1) + "-" +
                 std::to_string(std::min(_itemTotal, _itemPage * ITEMS_PER_PAGE + _first + rows)) + " of " +
                 std::to_string(_itemTotal),
             MARGIN, DisplayMgr::getInstance().getDisplay().height() - PAGER_H - 52, FONT_SIZE_SMALL, 300);
    }
    pager(DisplayMgr::getInstance().getDisplay().height() - PAGER_H - 44, int(_items.size()), rows);
}

void AppAudiobooks::drawDetail(int top) {
    Book32Display& d = DisplayMgr::getInstance().getDisplay();
    const int w = d.width();
    int y = top + 34;

    // A podcast: its episodes.
    if (_showPodcast) {
        y = paragraph(_item.title, MARGIN, y, FONT_SIZE_SUBTITLE, w - 2 * MARGIN, 2);
        const int rows = rowsFrom(y);
        for (int i = _first; i < int(_podcast.episodes.size()) && i < _first + rows; ++i, y += ROW_H) {
            const auto& e = _podcast.episodes[i];
            std::string detail = e.durationSec ? hours(e.durationSec) : "";
            if (e.finished) detail += ", finished";
            else if (e.progress > 0.005f) detail += ", " + std::to_string(int(e.progress * 100)) + "%";
            row(y, e.title, detail, Act::OpenEpisode, i);
        }
        pager(d.height() - PAGER_H - 44, int(_podcast.episodes.size()), rows);
        return;
    }

    y = paragraph(_item.title.empty() ? _detail.title : _item.title, MARGIN, y, FONT_SIZE_SUBTITLE, w - 2 * MARGIN, 3);
    const std::string author = _item.author.empty() ? _detail.author : _item.author;
    if (!author.empty()) y = paragraph(author, MARGIN, y, FONT_SIZE_BODY, w - 2 * MARGIN, 2);
    const uint32_t seconds = _detail.durationMs ? _detail.durationMs / 1000 : _item.durationSec;
    std::string facts;
    if (seconds) facts = hours(seconds);
    if (!_detail.chapters.empty()) facts += (facts.empty() ? "" : ", ") + std::to_string(_detail.chapters.size()) + " chapters";
    if (_item.progress > 0.005f && !_item.finished) facts += (facts.empty() ? "" : ", ") + std::to_string(int(_item.progress * 100)) + "% listened";
    if (!facts.empty()) { text(facts, MARGIN, y, FONT_SIZE_SMALL, w - 2 * MARGIN); y += 30; }

    AbsLocal::BookMeta meta;
    const bool local = AbsLocal::find(_item.id, meta);
    const bool complete = local && meta.complete;
    bool queued = false;
    std::string queueText;
    for (const auto& e : AbsQueue::entries()) {
        if (e.itemId != _item.id) continue;
        queued = true;
        queueText = queueState(e.state);
        if (!e.error.empty()) queueText += ": " + e.error;
    }
    if (complete) text("Downloaded to the card", MARGIN, y, FONT_SIZE_SMALL, w - 2 * MARGIN);
    else if (queued) text("Download: " + queueText, MARGIN, y, FONT_SIZE_SMALL, w - 2 * MARGIN);
    y += 30;

    const int bw = w - 2 * MARGIN;
    if (complete) {
        button(MARGIN, y, bw, BUTTON_H, "Play", Act::PlayDownloaded, 0, true);
        y += BUTTON_H + 16;
    }
    button(MARGIN, y, bw, BUTTON_H, complete ? "Stream instead" : "Stream", Act::Stream, 0, !complete);
    y += BUTTON_H + 16;
    if (!complete && !queued) {
        const int half = (w - 3 * MARGIN) / 2;
        button(MARGIN, y, half, BUTTON_H, "Download", Act::Queue);
        button(2 * MARGIN + half, y, half, BUTTON_H, "Download now", Act::QueueNow);
        y += BUTTON_H + 16;
        paragraph("Download waits for the charger; Download now starts on battery.", MARGIN, y + 20, FONT_SIZE_SMALL, bw, 2);
    }
    if (local || queued) button(MARGIN, y, bw, BUTTON_H, "Remove download", Act::RemoveDownload);
}

void AppAudiobooks::drawDownloads(int top) {
    Book32Display& d = DisplayMgr::getInstance().getDisplay();
    const int w = d.width();
    const auto entries = AbsQueue::entries();
    int y = top + 26;
    text(AbsQueue::charging() ? "On the charger: downloads run" : "On battery: downloads wait for the charger", MARGIN, y,
         FONT_SIZE_SMALL, w - 2 * MARGIN);
    y += 20;
    if (entries.empty()) {
        paragraph("Nothing queued. Open a book on the Server and choose Download.", MARGIN, y + 40, FONT_SIZE_BODY,
                  w - 2 * MARGIN, 4);
        return;
    }
    const int rowH = ROW_H + 60;
    const int rows = std::max(1, (d.height() - y - PAGER_H - 40) / rowH);
    const int third = (w - 4 * MARGIN) / 3;
    for (int i = _first; i < int(entries.size()) && i < _first + rows; ++i, y += rowH) {
        const auto& e = entries[i];
        text(e.title, MARGIN, y + 30, FONT_SIZE_MENU, w - 2 * MARGIN);
        std::string state = queueState(e.state);
        if (e.now && e.state == AbsQueue::State::Waiting) state = "Next";
        if (!e.error.empty()) state += ": " + e.error;
        text(state, MARGIN, y + 56, FONT_SIZE_SMALL, w - 2 * MARGIN);
        const int by = y + 68;
        if (e.state != AbsQueue::State::Downloading) button(MARGIN, by, third, 52, "Start now", Act::StartNow, i);
        if (e.state != AbsQueue::State::Paused) button(2 * MARGIN + third, by, third, 52, "Pause", Act::Pause, i);
        button(3 * MARGIN + 2 * third, by, third, 52, "Remove", Act::Remove, i);
    }
    if (int(entries.size()) > rows) pager(d.height() - PAGER_H - 44, int(entries.size()), rows);
}

#endif
