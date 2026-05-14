// ============================================================
//  L3_orderflow.cpp  —  Standalone L3 Order Flow Visualizer
//  Language : C++17
//  Build    : g++ -std=c++17 -O2 -o market L3_orderflow.cpp
//  Run      : ./market  OR  just hit Run in Code::Blocks
//
//  CSV columns (tab OR comma, auto-detected):
//    timestamp | action | side | price | size | order_id | [source]
// ============================================================

// ─────────────────────────────────────────────────────────────
//  SECTION 1 — INCLUDES
// ─────────────────────────────────────────────────────────────
#include <iostream>
#include <map>
#include <unordered_map>
#include <string>
#include <vector>
#include <deque>
#include <algorithm>
#include <chrono>
#include <thread>
#include <iomanip>
#include <sstream>
#include <fstream>
#include <cstdlib>
#include <ctime>
#include <cmath>

// ─────────────────────────────────────────────────────────────
//  SECTION 2 — CONSTANTS
// ─────────────────────────────────────────────────────────────
static constexpr int BOOK_DEPTH = 20;
static constexpr int REFRESH_MS = 10;
static constexpr int TAPE_ROWS  = BOOK_DEPTH * 2 + 1;   // 21

// Terminal layout
static constexpr int TAPE_W  = 32;
static constexpr int DOM_COL = TAPE_W + 2;
static constexpr int DOM_W   = 64;

// DOM sub-column offsets (0 = first char of DOM panel)
static constexpr int D_B_ORD = 0;
static constexpr int D_B_BAR = 4;
static constexpr int D_B_SZ  = 17;
static constexpr int D_PRICE = 26;
static constexpr int D_A_SZ  = 36;
static constexpr int D_A_BAR = 45;
static constexpr int D_A_ORD = 58;

static constexpr int MAX_BID_BAR = D_B_SZ  - D_B_BAR - 1;   // 12
static constexpr int MAX_ASK_BAR = D_A_ORD - D_A_BAR - 1;   // 12

// Screen rows
static constexpr int R_TITLE  = 1;
static constexpr int R_STATS  = 2;
static constexpr int R_HDR    = 3;
static constexpr int R_ASK_0  = 4;
static constexpr int R_SPREAD = R_ASK_0  + BOOK_DEPTH;   // 14
static constexpr int R_BID_0  = R_SPREAD + 1;            // 15
static constexpr int R_DBAR   = R_BID_0  + BOOK_DEPTH;   // 25
static constexpr int R_CURSOR = R_DBAR   + 2;            // 27

// ─────────────────────────────────────────────────────────────
//  SECTION 3 — ENUMS
// ─────────────────────────────────────────────────────────────
enum class Side      { BUY, SELL };
enum class EventType { ADD, CANCEL, MODIFY, TRADE, FILL, RESET };

// ─────────────────────────────────────────────────────────────
//  SECTION 4 — DATA STRUCTURES
// ─────────────────────────────────────────────────────────────
struct Order
{
    uint64_t    order_id;
    Side        side;
    double      price;
    int64_t     size;
    int64_t     orig_size;
    int64_t     timestamp_ns;
    std::string source;
    int         fill_count   = 0;
    int         modify_count = 0;
    int64_t     filled_size  = 0;
};

struct PriceLevel
{
    double                price       = 0.0;
    int64_t               total_size  = 0;
    int                   order_count = 0;
    std::vector<uint64_t> order_ids;
};

struct Trade
{
    uint64_t    trade_id;
    double      price;
    int64_t     size;
    Side        aggressor_side;
    int64_t     timestamp_ns;
    std::string time_str;
};

struct FeedMessage
{
    int64_t     timestamp_ns;
    EventType   event_type;
    Side        side;
    double      price;
    int64_t     size;
    uint64_t    order_id;
    std::string source;
    std::string time_str;
};

// ─────────────────────────────────────────────────────────────
//  SECTION 5 — ORDER BOOK
// ─────────────────────────────────────────────────────────────
class OrderBook
{
public:
    std::map<double, PriceLevel, std::greater<double>> bids;
    std::map<double, PriceLevel>                       asks;
    std::unordered_map<uint64_t, Order>                order_map;
    std::deque<Trade>                                  tape;

    uint64_t next_trade_id   = 1;
    double   last_price      = 0.0;
    double   prev_last_price = 0.0;
    int64_t  total_vol       = 0;
    int64_t  buy_vol         = 0;
    int64_t  sell_vol        = 0;
    int64_t  cum_delta       = 0;

private:
    static void erase_id(std::vector<uint64_t>& v, uint64_t id)
    {
        v.erase(std::remove(v.begin(), v.end(), id), v.end());
    }

    void detach(Side side, double price, int64_t sz, uint64_t id)
    {
        if (side == Side::BUY)
        {
            auto it = bids.find(price);
            if (it == bids.end()) return;
            it->second.total_size -= sz;
            erase_id(it->second.order_ids, id);
            if (--it->second.order_count <= 0) bids.erase(it);
        }
        else
        {
            auto it = asks.find(price);
            if (it == asks.end()) return;
            it->second.total_size -= sz;
            erase_id(it->second.order_ids, id);
            if (--it->second.order_count <= 0) asks.erase(it);
        }
    }

public:
    void add_order(const FeedMessage& m)
    {
        Order o;
        o.order_id = m.order_id; o.side = m.side; o.price = m.price;
        o.size = o.orig_size = m.size;
        o.timestamp_ns = m.timestamp_ns; o.source = m.source;
        order_map[o.order_id] = o;

        if (m.side == Side::BUY)
        {
            auto& lvl = bids[m.price];
            lvl.price = m.price; lvl.total_size += m.size;
            lvl.order_count++; lvl.order_ids.push_back(m.order_id);
        }
        else
        {
            auto& lvl = asks[m.price];
            lvl.price = m.price; lvl.total_size += m.size;
            lvl.order_count++; lvl.order_ids.push_back(m.order_id);
        }
    }

    void cancel_order(const FeedMessage& m)
    {
        auto it = order_map.find(m.order_id);
        if (it == order_map.end()) return;
        Order& o = it->second;
        detach(o.side, o.price, o.size, m.order_id);
        order_map.erase(it);
    }

    void modify_order(const FeedMessage& m)
    {
        auto it = order_map.find(m.order_id);
        if (it == order_map.end()) return;
        Order& o   = it->second;
        bool moved = (m.price != o.price);

        if (o.side == Side::BUY)
        {
            auto lit = bids.find(o.price);
            if (lit != bids.end())
            {
                lit->second.total_size -= o.size;
                if (moved)
                {
                    erase_id(lit->second.order_ids, m.order_id);
                    if (--lit->second.order_count <= 0) bids.erase(lit);
                }
            }
            auto& nl = bids[m.price];
            nl.price = m.price; nl.total_size += m.size;
            if (moved) { nl.order_count++; nl.order_ids.push_back(m.order_id); }
        }
        else
        {
            auto lit = asks.find(o.price);
            if (lit != asks.end())
            {
                lit->second.total_size -= o.size;
                if (moved)
                {
                    erase_id(lit->second.order_ids, m.order_id);
                    if (--lit->second.order_count <= 0) asks.erase(lit);
                }
            }
            auto& nl = asks[m.price];
            nl.price = m.price; nl.total_size += m.size;
            if (moved) { nl.order_count++; nl.order_ids.push_back(m.order_id); }
        }

        o.price = m.price; o.size = m.size; o.modify_count++;
    }

    void handle_trade(const FeedMessage& m)
    {
        Trade t;
        t.trade_id = next_trade_id++; t.price = m.price; t.size = m.size;
        t.aggressor_side = m.side; t.timestamp_ns = m.timestamp_ns;
        t.time_str = m.time_str;

        tape.push_front(t);
        while ((int)tape.size() > TAPE_ROWS) tape.pop_back();

        prev_last_price = last_price;
        last_price      = m.price;
        total_vol      += m.size;
        if (m.side == Side::BUY) { buy_vol  += m.size; cum_delta += m.size; }
        else                     { sell_vol += m.size; cum_delta -= m.size; }
    }

    void handle_fill(const FeedMessage& m)
    {
        auto it = order_map.find(m.order_id);
        if (it == order_map.end()) return;
        Order& o = it->second;

        o.size -= m.size; o.fill_count++; o.filled_size += m.size;

        if (o.side == Side::BUY)
        {
            auto lit = bids.find(o.price);
            if (lit != bids.end()) lit->second.total_size -= m.size;
        }
        else
        {
            auto lit = asks.find(o.price);
            if (lit != asks.end()) lit->second.total_size -= m.size;
        }

        if (o.size <= 0)
        {
            detach(o.side, o.price, 0, m.order_id);
            order_map.erase(it);
        }
    }

    void handle_reset()
    {
        bids.clear(); asks.clear(); order_map.clear();
    }

    void process(const FeedMessage& m)
    {
        switch (m.event_type)
        {
            case EventType::ADD:    add_order(m);    break;
            case EventType::CANCEL: cancel_order(m); break;
            case EventType::MODIFY: modify_order(m); break;
            case EventType::TRADE:  handle_trade(m); break;
            case EventType::FILL:   handle_fill(m);  break;
            case EventType::RESET:  handle_reset();  break;
        }
    }

    double best_bid()  const { return bids.empty() ? 0.0 : bids.begin()->first; }
    double best_ask()  const { return asks.empty() ? 0.0 : asks.begin()->first; }
    double spread()    const
    {
        return (best_bid() > 0 && best_ask() > 0) ? best_ask() - best_bid() : 0.0;
    }
    double mid_price() const { return (best_bid() + best_ask()) * 0.5; }

    double order_imbalance(int levels = 5) const
    {
        int64_t bs = 0, as = 0; int n = 0;
        for (auto& [p,l] : bids) { bs += l.total_size; if (++n >= levels) break; }
        n = 0;
        for (auto& [p,l] : asks) { as += l.total_size; if (++n >= levels) break; }
        return (bs + as == 0) ? 0.0 : (double)(bs - as) / (double)(bs + as);
    }
};

// ─────────────────────────────────────────────────────────────
//  SECTION 6 — CSV FEED
// ─────────────────────────────────────────────────────────────
class CSVFeed
{
public:
    bool open(const std::string& path)
    {
        file_.open(path);
        if (!file_.is_open()) return false;
        std::getline(file_, header_);
        if (!header_.empty() && header_.back() == '\r') header_.pop_back();
        delim_ = (header_.find('\t') != std::string::npos) ? '\t' : ',';
        return true;
    }

    bool next(FeedMessage& msg)
    {
        std::string line;
        while (std::getline(file_, line))
        {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty() || line[0] == '#') continue;
            if (parse(line, msg)) return true;
        }
        return false;
    }

private:
    std::ifstream file_;
    std::string   header_;
    char          delim_ = '\t';

    bool parse(const std::string& line, FeedMessage& msg)
    {
        std::vector<std::string> f;
        std::istringstream ss(line);
        std::string tok;
        while (std::getline(ss, tok, delim_)) f.push_back(tok);
        if (f.size() < 6) return false;

        // Timestamp
        const std::string& ts = f[0];
        size_t sep = ts.find('T');
        if (sep == std::string::npos) sep = ts.find(' ');
        msg.time_str = (sep != std::string::npos && sep + 8 < ts.size())
                       ? ts.substr(sep + 1, 8) : "??:??:??";

        size_t dot = ts.find('.');
        std::string sec_part  = (dot != std::string::npos) ? ts.substr(0, dot) : ts;
        std::string frac_part = (dot != std::string::npos) ? ts.substr(dot + 1) : "";

        for (size_t i = 0; i < frac_part.size(); i++)
        {
            char c = frac_part[i];
            if (c == '+' || c == '-' || c == 'Z')
            { frac_part = frac_part.substr(0, i); break; }
        }
        while ((int)frac_part.size() < 9) frac_part += '0';
        frac_part = frac_part.substr(0, 9);

        for (char& c : sec_part) if (c == 'T') c = ' ';
        std::tm tm = {};
        { std::istringstream tss(sec_part); tss >> std::get_time(&tm, "%Y-%m-%d %H:%M:%S"); }
        tm.tm_isdst = -1;
        msg.timestamp_ns = (int64_t)std::mktime(&tm) * 1'000'000'000LL
                           + std::stoll(frac_part);

        // Action — case insensitive
        std::string a = f[1];
        for (char& c : a) c = (char)std::tolower((unsigned char)c);

        if      (a == "add")                                      msg.event_type = EventType::ADD;
        else if (a == "cancel" || a == "cancelled")               msg.event_type = EventType::CANCEL;
        else if (a == "modify" || a == "modified" || a == "update")msg.event_type = EventType::MODIFY;
        else if (a == "trade"  || a == "exec" || a == "matched")  msg.event_type = EventType::TRADE;
        else if (a == "fill"   || a == "filled")                  msg.event_type = EventType::FILL;
        else if (a == "reset"  || a == "clear")                   msg.event_type = EventType::RESET;
        else return false;

        // Side
        const std::string& sd = f[2];
        msg.side = (sd == "Buy" || sd == "buy" || sd == "BUY")
                   ? Side::BUY : Side::SELL;

        // Numerics
        try
        {
            msg.price    = f[3].empty() ? 0.0 : std::stod(f[3]);
            msg.size     = f[4].empty() ? 0   : (int64_t)std::stod(f[4]);
            msg.order_id = f[5].empty() ? 0   : (uint64_t)std::stod(f[5]);
        }
        catch (...) { return false; }

        msg.source = (f.size() > 6) ? f[6] : "";
        return true;
    }
};

// ─────────────────────────────────────────────────────────────
//  SECTION 7 — COLOURS
// ─────────────────────────────────────────────────────────────
namespace clr
{
    static const std::string R = "\033[31m";
    static const std::string G = "\033[32m";
    static const std::string Y = "\033[33m";
    static const std::string C = "\033[36m";
    static const std::string W = "\033[37m";
    static const std::string X = "\033[0m";
    static const std::string B = "\033[1m";
    static const std::string D = "\033[2m";
}

// ─────────────────────────────────────────────────────────────
//  SECTION 8 — VISUALIZER
// ─────────────────────────────────────────────────────────────
class Visualizer
{
public:
    bool initialized = false;

    ~Visualizer()
    {
        std::cout << "\033[?25h\033[" << R_CURSOR << ";1H\n" << std::flush;
    }

    void render(const OrderBook& book)
    {
        if (!initialized) draw_chrome();
        draw_title(book);
        draw_stats(book);
        draw_asks(book);
        draw_spread(book);
        draw_bids(book);
        draw_tape(book);
        draw_imbalance_bar(book);
        move(R_CURSOR, 1);
        std::cout.flush();
    }

private:
    void move(int r, int c) { std::cout << "\033[" << r << ";" << c << "H"; }
    void at_dom(int r, int off) { move(r, DOM_COL + off); }

    void cell(const std::string& s, int w, bool right = true)
    {
        int len = (int)s.size();
        if (len >= w) { std::cout << s.substr(0, w); return; }
        int pad = w - len;
        if (right) std::cout << std::string(pad, ' ') << s;
        else       std::cout << s << std::string(pad, ' ');
    }

    static std::string fp(double v, int p = 2)
    {
        std::ostringstream s; s << std::fixed << std::setprecision(p) << v;
        return s.str();
    }
    static std::string fi(int64_t v) { return std::to_string(v); }

    void draw_chrome()
    {
        std::cout << "\033[2J\033[?25l";

        for (int r = R_TITLE; r <= R_CURSOR; r++)
        { move(r, TAPE_W + 1); std::cout << clr::D << "|" << clr::X; }

        // DOM headers
        move(R_HDR, DOM_COL + D_B_ORD);
        std::cout << clr::B << clr::G;
        std::cout << "ORD";
        at_dom(R_HDR, D_B_BAR);  cell("[BID DEPTH  ]", MAX_BID_BAR, false);
        at_dom(R_HDR, D_B_SZ);   cell("BID SZ", 8);
        at_dom(R_HDR, D_PRICE);  std::cout << clr::Y; cell("  PRICE  ", 9, false);
        std::cout << clr::R;
        at_dom(R_HDR, D_A_SZ);   cell(" ASK SZ", 8, false);
        at_dom(R_HDR, D_A_BAR);  cell("[ASK DEPTH  ]", MAX_ASK_BAR, false);
        at_dom(R_HDR, D_A_ORD);  std::cout << "ORD";
        std::cout << clr::X;

        // Tape header
        move(R_HDR, 1);
        std::cout << clr::B << clr::C << " TIME       SIZE    PRICE" << clr::X;

        // Blank all data rows
        for (int r = R_ASK_0; r <= R_DBAR; r++)
        {
            move(r, 1);       std::cout << std::string(TAPE_W, ' ');
            move(r, DOM_COL); std::cout << std::string(DOM_W,  ' ');
        }

        initialized = true;
    }

    void draw_title(const OrderBook& book)
    {
        move(R_TITLE, 1);
        std::cout << std::string(TAPE_W + 1 + DOM_W, ' ');
        move(R_TITLE, 1);

        double last = book.last_price, prev = book.prev_last_price;
        std::cout << clr::B << clr::C << " DOM" << clr::X;
        std::cout << "  Last: " << clr::B;
        if      (last > prev) std::cout << clr::G << fp(last) << " [+]" << clr::X;
        else if (last < prev) std::cout << clr::R << fp(last) << " [-]" << clr::X;
        else                  std::cout << clr::W << fp(last) << " [=]" << clr::X;
        std::cout << "  Spread: " << clr::Y << fp(book.spread()) << clr::X;
        std::cout << "  Mid: "    << clr::Y << fp(book.mid_price()) << clr::X;
    }

    void draw_stats(const OrderBook& book)
    {
        move(R_STATS, 1);
        std::cout << std::string(TAPE_W + 1 + DOM_W, ' ');
        move(R_STATS, 1);

        std::string dc = (book.cum_delta >= 0) ? clr::G : clr::R;
        std::string dp = (book.cum_delta >= 0) ? "+" : "";

        std::cout << clr::D << " Vol:" << clr::X << fi(book.total_vol)
                  << "  " << clr::G << "Buy:"  << fi(book.buy_vol)  << clr::X
                  << "  " << clr::R << "Sell:" << fi(book.sell_vol) << clr::X
                  << "  Delta:" << dc << dp << fi(book.cum_delta) << clr::X;
    }

    // ── FIXED: idx = BOOK_DEPTH - 1 - slot ───────────────────
    // slot 0 (top)   = worst ask (highest price)
    // slot 9 (bottom)= best  ask (lowest  price, closest to spread)
    void draw_asks(const OrderBook& book)
    {
        std::vector<std::pair<double, PriceLevel>> lvls(
            book.asks.begin(), book.asks.end());
        int count = std::min((int)lvls.size(), BOOK_DEPTH);

        double max_sz = 1.0;
        for (int i = 0; i < count; i++)
            max_sz = std::max(max_sz, (double)lvls[i].second.total_size);

        for (int slot = 0; slot < BOOK_DEPTH; slot++)
        {
            int idx = BOOK_DEPTH - 1 - slot;   // FIXED
            int row = R_ASK_0 + slot;

            move(row, DOM_COL);
            std::cout << std::string(DOM_W, ' ');

            if (idx >= count || idx < 0) continue;

            auto& [price, lvl] = lvls[idx];
            int bar = std::max(1, (int)((double)lvl.total_size / max_sz * MAX_ASK_BAR));

            std::cout << clr::R;
            at_dom(row, D_PRICE); cell(fp(price),          9);
            at_dom(row, D_A_SZ);  cell(fi(lvl.total_size), 8);
            at_dom(row, D_A_BAR); std::cout << std::string(bar, '#');
            at_dom(row, D_A_ORD); cell(fi(lvl.order_count), 3, false);
            std::cout << clr::X;
        }
    }

    void draw_spread(const OrderBook& book)
    {
        move(R_SPREAD, DOM_COL);
        std::cout << std::string(DOM_W, ' ');
        move(R_SPREAD, DOM_COL);

        std::ostringstream s;
        s << std::fixed << std::setprecision(2);
        s << "  --- SPREAD " << book.spread()
          << "  MID " << book.mid_price() << " ---";

        std::cout << clr::B << clr::Y;
        cell(s.str(), DOM_W, false);
        std::cout << clr::X;
    }

    // ── FIXED: bar capped at MAX_BID_BAR so it never overwrites SIZE column
    void draw_bids(const OrderBook& book)
    {
        std::vector<std::pair<double, PriceLevel>> lvls(
            book.bids.begin(), book.bids.end());
        int count = std::min((int)lvls.size(), BOOK_DEPTH);

        double max_sz = 1.0;
        for (int i = 0; i < count; i++)
            max_sz = std::max(max_sz, (double)lvls[i].second.total_size);

        for (int slot = 0; slot < BOOK_DEPTH; slot++)
        {
            int row = R_BID_0 + slot;
            move(row, DOM_COL);
            std::cout << std::string(DOM_W, ' ');

            if (slot >= count) continue;

            auto& [price, lvl] = lvls[slot];
            int bar = std::max(1, (int)((double)lvl.total_size / max_sz * MAX_BID_BAR));  // FIXED

            std::cout << clr::G;
            at_dom(row, D_B_ORD); cell(fi(lvl.order_count), 3);
            at_dom(row, D_B_BAR); std::cout << std::string(bar, '#');
            at_dom(row, D_B_SZ);  cell(fi(lvl.total_size), 8);
            at_dom(row, D_PRICE); cell(fp(price),           9);
            std::cout << clr::X;
        }
    }

    void draw_tape(const OrderBook& book)
    {
        for (int i = 0; i < TAPE_ROWS; i++)
        {
            int row = R_ASK_0 + i;
            move(row, 1);
            std::cout << std::string(TAPE_W, ' ');
            move(row, 1);

            if (i >= (int)book.tape.size()) continue;
            const Trade& t = book.tape[i];
            bool buy = (t.aggressor_side == Side::BUY);

            std::cout << (buy ? clr::G : clr::R);
            std::cout << " " << t.time_str << " ";
            cell(fi(t.size), 7);
            std::cout << "  ";
            cell(fp(t.price), 8);
            std::cout << (buy ? " ^" : " v") << clr::X;
        }
    }

    void draw_imbalance_bar(const OrderBook& book)
    {
        double imb = book.order_imbalance(5);
        const int BAR = 36, mid = BAR / 2;
        int pos = (int)((imb + 1.0) * 0.5 * BAR);
        pos = std::max(0, std::min(BAR, pos));

        move(R_DBAR, DOM_COL);
        std::cout << std::string(DOM_W, ' ');
        move(R_DBAR, DOM_COL);
        std::cout << clr::D << " IMB[" << clr::X;

        for (int i = 0; i < BAR; i++)
        {
            bool sell_fill = (pos < mid && i >= pos && i < mid);
            bool buy_fill  = (pos > mid && i >= mid && i < pos);

            if      (i == mid)  std::cout << clr::Y << "|" << clr::X;
            else if (sell_fill) std::cout << clr::R << "#" << clr::X;
            else if (buy_fill)  std::cout << clr::G << "#" << clr::X;
            else                std::cout << clr::D << "-" << clr::X;
        }

        std::cout << clr::D << "]" << clr::X;
        std::string dp = (book.cum_delta >= 0) ? "+" : "";
        std::string dc = (book.cum_delta >= 0) ? clr::G : clr::R;
        std::cout << "  " << dc << "D:" << dp << fi(book.cum_delta) << clr::X;

        std::ostringstream imbs;
        imbs << std::fixed << std::setprecision(3) << imb;
        std::cout << clr::D << "  imb:" << clr::X
                  << ((imb >= 0) ? clr::G : clr::R) << imbs.str() << clr::X;
    }
};

// ─────────────────────────────────────────────────────────────
//  SECTION 9 — MAIN
// ─────────────────────────────────────────────────────────────
int main(int argc, char* argv[])
{
    // ── Change this path to your CSV file ────────────────────
    const std::string FALLBACK_PATH =
        "D:\\C++_PROJECTS\\L3 OF VISUALISER\\L3 Visualizer\\tmpar_yjsl7.csv";

    std::string csv_path;
    if (argc >= 2)
        csv_path = argv[1];
    else
        csv_path = FALLBACK_PATH;

    OrderBook  book;
    CSVFeed    feed;
    Visualizer viz;

    if (!feed.open(csv_path))
    {
        std::cerr << "Cannot open: " << csv_path << "\n";
        std::cerr << "Update FALLBACK_PATH in main() to your CSV location.\n";
        return 1;
    }

    std::cout << "Opened: " << csv_path << "\nStarting...\n";
    std::this_thread::sleep_for(std::chrono::milliseconds(800));

    FeedMessage msg;
    int64_t prev_ts = 0;

    while (feed.next(msg))
    {
        if (msg.event_type == EventType::RESET)
            viz.initialized = false;

        book.process(msg);

        if (msg.timestamp_ns - prev_ts >= 100'000'000LL
            && !book.bids.empty()
            && !book.asks.empty())
        {
            viz.render(book);
            std::this_thread::sleep_for(std::chrono::milliseconds(REFRESH_MS));
            prev_ts = msg.timestamp_ns;
        }
    }

    viz.render(book);
    std::cout << "\n[END OF FEED]\n";
    return 0;
}
