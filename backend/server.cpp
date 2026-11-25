#include "hash_core.h"

#include <algorithm>
#include <cctype>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <list>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #pragma comment(lib, "ws2_32.lib")
  using SocketType = SOCKET;
  #define CLOSE_SOCKET closesocket
#else
  #include <netinet/in.h>
  #include <sys/socket.h>
  #include <unistd.h>
  using SocketType = int;
  constexpr SocketType INVALID_SOCKET = -1;
  #define CLOSE_SOCKET close
#endif

using namespace std;

// ============================================================
// 1. DATA MODEL
// ============================================================
struct Restaurant {
    string id;
    string name;
    string tagline;
};

struct MenuItem {
    string id;
    string restaurantId;
    string name;
    string category;
    double price{};
};

// ============================================================
// 2. HASH TABLE — Separate Chaining (Calls C hash_core.c)
// ============================================================
template <typename V>
class HashTable {
    struct Entry {
        string key;
        V value;
    };
    vector<list<Entry>> buckets;

    static string norm(const string& k) {
        char buf[256];
        normalize_key(k.c_str(), buf, sizeof(buf));
        return string(buf);
    }

public:
    explicit HashTable(size_t size = 31) : buckets(size) {}

    size_t hash(const string& k) const {
        return compute_hash_bucket(k.c_str(), buckets.size());
    }

    size_t bucketCount() const { return buckets.size(); }

    size_t chainSizeAt(const string& k) const {
        return buckets[hash(k)].size();
    }

    void set(const string& key, const V& value) {
        string nk = norm(key);
        auto& chain = buckets[hash(nk)];
        for (auto& e : chain) {
            if (e.key == nk) {
                e.value = value;
                return;
            }
        }
        chain.push_back({nk, value});
    }

    V* get(const string& key) {
        string nk = norm(key);
        for (auto& e : buckets[hash(nk)]) {
            if (e.key == nk) return &e.value;
        }
        return nullptr;
    }

    vector<V> allValues() const {
        vector<V> out;
        for (const auto& b : buckets) {
            for (const auto& e : b) out.push_back(e.value);
        }
        return out;
    }
};

// ============================================================
// 3. LINKED LIST — Dynamic Shopping Cart
// ============================================================
struct CartNode {
    MenuItem item;
    int qty;
    CartNode* next;
    CartNode(const MenuItem& m, int q) : item(m), qty(q), next(nullptr) {}
};

class Cart {
    CartNode* head = nullptr;

public:
    ~Cart() { clear(); }
    Cart() = default;
    Cart(const Cart&) = delete;
    Cart& operator=(const Cart&) = delete;

    int qtyOf(const string& id) const {
        for (CartNode* c = head; c; c = c->next) {
            if (c->item.id == id) return c->qty;
        }
        return 0;
    }

    // Updates qty, unlinks if q <= 0, or appends a new node. Returns old qty.
    int setQty(const MenuItem& m, int q) {
        CartNode* prev = nullptr;
        for (CartNode* c = head; c; prev = c, c = c->next) {
            if (c->item.id != m.id) continue;
            int old = c->qty;
            if (q <= 0) {
                (prev ? prev->next : head) = c->next;
                delete c;
            } else {
                c->qty = q;
            }
            return old;
        }
        if (q > 0) {
            CartNode* n = new CartNode(m, q);
            if (!head) {
                head = n;
            } else {
                CartNode* t = head;
                while (t->next) t = t->next;
                t->next = n;
            }
        }
        return 0;
    }

    double total() const {
        double sum = 0.0;
        for (CartNode* c = head; c; c = c->next) {
            sum += c->item.price * c->qty;
        }
        return sum;
    }

    int lineCount() const {
        int n = 0;
        for (CartNode* c = head; c; c = c->next) n++;
        return n;
    }

    vector<pair<MenuItem, int>> lines() const {
        vector<pair<MenuItem, int>> v;
        for (CartNode* c = head; c; c = c->next) {
            v.push_back({c->item, c->qty});
        }
        return v;
    }

    void clear() {
        while (head) {
            CartNode* n = head->next;
            delete head;
            head = n;
        }
    }
};

// ============================================================
// 4. STACK — Recent Actions & Undo (LIFO)
// ============================================================
enum class ActionType { ADD, REMOVE, CHANGE_QTY };

struct Action {
    ActionType type;
    MenuItem item;
    int prevQty;
    int newQty;
};

class ActionStack {
    struct Node {
        Action a;
        Node* next;
    };
    Node* topNode = nullptr;

public:
    ~ActionStack() { clear(); }
    ActionStack() = default;
    ActionStack(const ActionStack&) = delete;
    ActionStack& operator=(const ActionStack&) = delete;

    void push(const Action& a) { topNode = new Node{a, topNode}; }

    bool pop(Action& out) {
        if (!topNode) return false;
        Node* n = topNode;
        out = n->a;
        topNode = n->next;
        delete n;
        return true;
    }

    const Action* peek() const { return topNode ? &topNode->a : nullptr; }

    vector<Action> toVector() const {
        vector<Action> out;
        for (Node* c = topNode; c; c = c->next) out.push_back(c->a);
        return out;
    }

    void clear() {
        Action tmp;
        while (pop(tmp)) {}
    }
};

// ============================================================
// 5. QUEUE — Order Processing (FIFO)
// ============================================================
enum class Status { PENDING, PREPARING, READY, COMPLETED };

struct Order {
    int id{};
    vector<pair<MenuItem, int>> lines;
    double total{};
    Status status{Status::PENDING};
};

class OrderQueue {
    struct Node {
        Order o;
        Node* next;
    };
    Node* frontNode = nullptr;
    Node* rearNode = nullptr;
    int count = 0;

public:
    ~OrderQueue() {
        Order tmp;
        while (dequeue(tmp)) {}
    }
    OrderQueue() = default;
    OrderQueue(const OrderQueue&) = delete;
    OrderQueue& operator=(const OrderQueue&) = delete;

    void enqueue(const Order& o) {
        Node* n = new Node{o, nullptr};
        if (rearNode) rearNode->next = n;
        else frontNode = n;
        rearNode = n;
        count++;
    }

    bool dequeue(Order& out) {
        if (!frontNode) return false;
        Node* n = frontNode;
        out = n->o;
        frontNode = n->next;
        if (!frontNode) rearNode = nullptr;
        delete n;
        count--;
        return true;
    }

    Order* front() { return frontNode ? &frontNode->o : nullptr; }
    int size() const { return count; }

    vector<Order> toVector() const {
        vector<Order> out;
        for (Node* c = frontNode; c; c = c->next) out.push_back(c->o);
        return out;
    }
};

// ============================================================
// 6. ORDERING SESSION — Combines All 4 Structures
// ============================================================
class OrderingSession {
public:
    HashTable<Restaurant> restaurants;
    HashTable<MenuItem> menu;
    HashTable<Order> orderRecords;
    Cart cart;
    ActionStack history;
    OrderQueue orders;
    int nextOrderId = 1;

    OrderingSession() {
        restaurants.set("1042", {"1042", "Spice Atelier", "Modern Indian Kitchen"});
        restaurants.set("1055", {"1055", "Roast & Brew", "South Indian Filter Bar"});
        restaurants.set("1068", {"1068", "The Curry Leaf", "Comforting North Indian favorites"});
        restaurants.set("1081", {"1081", "Tandoor House", "Fresh from the clay oven"});

        menu.set("MENU-27", {"MENU-27", "1042", "Veg Biryani", "Mains", 180.0});
        menu.set("MENU-31", {"MENU-31", "1042", "Masala Dosa", "Classics", 90.0});
        menu.set("MENU-35", {"MENU-35", "1055", "Filter Coffee", "Beverages", 50.0});
        menu.set("MENU-42", {"MENU-42", "1042", "Paneer Tikka", "Starters", 220.0});
        menu.set("MENU-58", {"MENU-58", "1042", "Garlic Naan", "Breads", 60.0});
        menu.set("MENU-64", {"MENU-64", "1055", "Kesari Bath", "Desserts", 75.0});
        menu.set("MENU-66", {"MENU-66", "1042", "Samosa Chaat", "Starters", 110.0});
        menu.set("MENU-72", {"MENU-72", "1042", "Paneer Butter Masala", "Mains", 240.0});
        menu.set("MENU-79", {"MENU-79", "1042", "Gulab Jamun", "Desserts", 80.0});
        menu.set("MENU-83", {"MENU-83", "1055", "Idli Sambar", "Classics", 85.0});
        menu.set("MENU-87", {"MENU-87", "1055", "Medu Vada", "Starters", 90.0});
        menu.set("MENU-93", {"MENU-93", "1055", "Masala Chai", "Beverages", 45.0});
        menu.set("MENU-101", {"MENU-101", "1068", "Chole Bhature", "Classics", 170.0});
        menu.set("MENU-108", {"MENU-108", "1068", "Aloo Paratha", "Breads", 95.0});
        menu.set("MENU-114", {"MENU-114", "1068", "Dal Makhani", "Mains", 210.0});
        menu.set("MENU-121", {"MENU-121", "1068", "Mango Lassi", "Beverages", 80.0});
        menu.set("MENU-128", {"MENU-128", "1081", "Tandoori Chicken", "Mains", 320.0});
        menu.set("MENU-136", {"MENU-136", "1081", "Chicken 65", "Starters", 230.0});
    }

    void add(const MenuItem& m, int qty = 1) {
        int prev = cart.qtyOf(m.id);
        cart.setQty(m, prev + qty);
        history.push({ActionType::ADD, m, prev, prev + qty});
    }

    bool remove(const MenuItem& m) {
        int prev = cart.qtyOf(m.id);
        if (prev == 0) return false;
        cart.setQty(m, 0);
        history.push({ActionType::REMOVE, m, prev, 0});
        return true;
    }

    bool changeQty(const MenuItem& m, int q) {
        int prev = cart.qtyOf(m.id);
        if (prev == 0 || q <= 0) return false;
        cart.setQty(m, q);
        history.push({ActionType::CHANGE_QTY, m, prev, q});
        return true;
    }

    bool undo(Action& undoneAction) {
        if (!history.pop(undoneAction)) return false;
        cart.setQty(undoneAction.item, undoneAction.prevQty);
        return true;
    }

    void clearCart() {
        cart.clear();
        history.clear();
    }

    int checkout() {
        if (cart.lineCount() == 0) return -1;
        Order o{nextOrderId++, cart.lines(), cart.total(), Status::PENDING};
        orders.enqueue(o);
        orderRecords.set(to_string(o.id), o);
        clearCart();
        return o.id;
    }

    Status advanceFront(int& orderId) {
        Order* f = orders.front();
        if (!f) {
            orderId = -1;
            return Status::COMPLETED;
        }
        orderId = f->id;
        if (f->status != Status::COMPLETED) {
            f->status = static_cast<Status>(static_cast<int>(f->status) + 1);
        }
        Status s = f->status;
        orderRecords.set(to_string(f->id), *f);
        if (s == Status::COMPLETED) {
            Order done;
            orders.dequeue(done);
        }
        return s;
    }
};

// ============================================================
// 7. JSON SERIALIZATION & PARSING HELPERS
// ============================================================
string jsonEscape(const string& s) {
    string out;
    out.reserve(s.size());
    for (char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default: out += c; break;
        }
    }
    return out;
}

string formatMoney(double val) {
    ostringstream oss;
    oss << fixed << setprecision(2) << val;
    return oss.str();
}

string statusToString(Status s) {
    switch (s) {
        case Status::PENDING: return "Pending";
        case Status::PREPARING: return "Preparing";
        case Status::READY: return "Ready / out for delivery";
        case Status::COMPLETED: return "Completed";
    }
    return "Unknown";
}

string actionTypeToString(ActionType t) {
    switch (t) {
        case ActionType::ADD: return "ADD";
        case ActionType::REMOVE: return "REMOVE";
        case ActionType::CHANGE_QTY: return "CHANGE_QTY";
    }
    return "UNKNOWN";
}

string restaurantToJson(const Restaurant& r, size_t bucket, size_t chainLen) {
    ostringstream oss;
    oss << "{\"id\":\"" << jsonEscape(r.id)
        << "\",\"name\":\"" << jsonEscape(r.name)
        << "\",\"tagline\":\"" << jsonEscape(r.tagline)
        << "\",\"bucket\":" << bucket
        << ",\"chainLength\":" << chainLen << "}";
    return oss.str();
}

string menuItemToJson(const MenuItem& m, size_t bucket, size_t chainLen, const string& restaurantName) {
    ostringstream oss;
    oss << "{\"id\":\"" << jsonEscape(m.id)
        << "\",\"restaurantId\":\"" << jsonEscape(m.restaurantId)
        << "\",\"restaurantName\":\"" << jsonEscape(restaurantName)
        << "\",\"name\":\"" << jsonEscape(m.name)
        << "\",\"category\":\"" << jsonEscape(m.category)
        << "\",\"price\":" << formatMoney(m.price)
        << ",\"bucket\":" << bucket
        << ",\"chainLength\":" << chainLen << "}";
    return oss.str();
}

string actionToJson(const Action& a) {
    ostringstream oss;
    oss << "{\"type\":\"" << actionTypeToString(a.type)
        << "\",\"itemId\":\"" << jsonEscape(a.item.id)
        << "\",\"itemName\":\"" << jsonEscape(a.item.name)
        << "\",\"prevQty\":" << a.prevQty
        << ",\"newQty\":" << a.newQty << "}";
    return oss.str();
}

string orderToJson(const Order& o) {
    ostringstream oss;
    oss << "{\"id\":" << o.id
        << ",\"total\":" << formatMoney(o.total)
        << ",\"status\":\"" << statusToString(o.status)
        << "\",\"statusIndex\":" << static_cast<int>(o.status)
        << ",\"lines\":[";
    for (size_t i = 0; i < o.lines.size(); ++i) {
        if (i > 0) oss << ",";
        oss << "{\"id\":\"" << jsonEscape(o.lines[i].first.id)
            << "\",\"name\":\"" << jsonEscape(o.lines[i].first.name)
            << "\",\"price\":" << formatMoney(o.lines[i].first.price)
            << ",\"qty\":" << o.lines[i].second << "}";
    }
    oss << "]}";
    return oss.str();
}

string cartStateToJson(const OrderingSession& s) {
    auto lines = s.cart.lines();
    auto stackItems = s.history.toVector();
    ostringstream oss;
    oss << "{\"lineCount\":" << s.cart.lineCount()
        << ",\"total\":" << formatMoney(s.cart.total())
        << ",\"lines\":[";
    for (size_t i = 0; i < lines.size(); ++i) {
        if (i > 0) oss << ",";
        oss << "{\"id\":\"" << jsonEscape(lines[i].first.id)
            << "\",\"name\":\"" << jsonEscape(lines[i].first.name)
            << "\",\"price\":" << formatMoney(lines[i].first.price)
            << ",\"qty\":" << lines[i].second
            << ",\"subtotal\":" << formatMoney(lines[i].first.price * lines[i].second) << "}";
    }
    oss << "],\"undoStack\":[";
    for (size_t i = 0; i < stackItems.size(); ++i) {
        if (i > 0) oss << ",";
        oss << actionToJson(stackItems[i]);
    }
    oss << "]}";
    return oss.str();
}

string ordersStateToJson(OrderingSession& s) {
    auto active = s.orders.toVector();
    ostringstream oss;
    oss << "{\"queueSize\":" << s.orders.size() << ",\"queue\":[";
    for (size_t i = 0; i < active.size(); ++i) {
        if (i > 0) oss << ",";
        oss << orderToJson(active[i]);
    }
    oss << "],\"allOrders\":[";
    bool first = true;
    for (int id = 1; id < s.nextOrderId; ++id) {
        Order* ord = s.orderRecords.get(to_string(id));
        if (!ord) continue;
        if (!first) oss << ",";
        first = false;
        oss << orderToJson(*ord);
    }
    oss << "]}";
    return oss.str();
}

string extractJsonString(const string& body, const string& key) {
    string pattern = "\"" + key + "\"";
    size_t pos = body.find(pattern);
    if (pos == string::npos) return "";
    pos = body.find(':', pos + pattern.size());
    if (pos == string::npos) return "";
    pos++;
    while (pos < body.size() && isspace(static_cast<unsigned char>(body[pos]))) pos++;
    if (pos >= body.size() || body[pos] != '"') return "";
    pos++;
    string value;
    while (pos < body.size() && body[pos] != '"') {
        if (body[pos] == '\\' && pos + 1 < body.size()) pos++;
        value += body[pos++];
    }
    return value;
}

int extractJsonInt(const string& body, const string& key, int defaultVal) {
    string pattern = "\"" + key + "\"";
    size_t pos = body.find(pattern);
    if (pos == string::npos) return defaultVal;
    pos = body.find(':', pos + pattern.size());
    if (pos == string::npos) return defaultVal;
    pos++;
    while (pos < body.size() && isspace(static_cast<unsigned char>(body[pos]))) pos++;
    size_t start = pos;
    if (pos < body.size() && body[pos] == '-') pos++;
    while (pos < body.size() && isdigit(static_cast<unsigned char>(body[pos]))) pos++;
    if (pos == start) return defaultVal;
    try {
        return stoi(body.substr(start, pos - start));
    } catch (...) {
        return defaultVal;
    }
}

// ============================================================
// 8. HTTP SERVER UTILITIES
// ============================================================
struct HttpRequest {
    string method;
    string path;
    string body;
};

struct HttpResponse {
    int status = 200;
    string statusText = "OK";
    string contentType = "application/json";
    string body;
};

string readFile(const string& filepath) {
    ifstream f(filepath, ios::binary);
    if (!f) return "";
    ostringstream oss;
    oss << f.rdbuf();
    return oss.str();
}

string urlDecode(const string& s) {
    string out;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size()) {
            string hex = s.substr(i + 1, 2);
            out += static_cast<char>(strtol(hex.c_str(), nullptr, 16));
            i += 2;
        } else if (s[i] == '+') {
            out += ' ';
        } else {
            out += s[i];
        }
    }
    return out;
}

bool readHttpRequest(SocketType clientFd, HttpRequest& req) {
    string raw;
    char buf[4096];
    size_t headerEnd = string::npos;

    while (headerEnd == string::npos) {
        int n = static_cast<int>(recv(clientFd, buf, sizeof(buf), 0));
        if (n <= 0) return false;
        raw.append(buf, static_cast<size_t>(n));
        headerEnd = raw.find("\r\n\r\n");
        if (raw.size() > 65536) return false;
    }

    string headers = raw.substr(0, headerEnd);
    string body = raw.substr(headerEnd + 4);

    size_t contentLength = 0;
    istringstream hss(headers);
    string line;
    if (!getline(hss, line)) return false;
    {
        istringstream rl(line);
        string target;
        rl >> req.method >> target;
        size_t qpos = target.find('?');
        req.path = urlDecode(qpos == string::npos ? target : target.substr(0, qpos));
    }

    while (getline(hss, line)) {
        string lower = line;
        for (char& c : lower) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
        if (lower.rfind("content-length:", 0) == 0) {
            try {
                contentLength = static_cast<size_t>(stoul(lower.substr(15)));
            } catch (...) {
                contentLength = 0;
            }
        }
    }

    while (body.size() < contentLength) {
        int n = static_cast<int>(recv(clientFd, buf, sizeof(buf), 0));
        if (n <= 0) break;
        body.append(buf, static_cast<size_t>(n));
    }
    req.body = body.substr(0, contentLength);
    return true;
}

void sendHttpResponse(SocketType clientFd, const HttpResponse& res) {
    ostringstream oss;
    oss << "HTTP/1.1 " << res.status << " " << res.statusText << "\r\n"
        << "Content-Type: " << res.contentType << "; charset=utf-8\r\n"
        << "Content-Length: " << res.body.size() << "\r\n"
        << "Access-Control-Allow-Origin: *\r\n"
        << "Access-Control-Allow-Methods: GET, POST, PUT, DELETE, OPTIONS\r\n"
        << "Access-Control-Allow-Headers: Content-Type\r\n"
        << "Connection: close\r\n\r\n"
        << res.body;

    string payload = oss.str();
    size_t totalSent = 0;
    while (totalSent < payload.size()) {
        int sent = static_cast<int>(
            send(clientFd, payload.c_str() + totalSent, static_cast<int>(payload.size() - totalSent), 0));
        if (sent <= 0) break;
        totalSent += static_cast<size_t>(sent);
    }
}

// ============================================================
// 9. API ROUTER
// ============================================================
HttpResponse handleRequest(const HttpRequest& req, OrderingSession& session) {
    if (req.method == "OPTIONS") return {204, "No Content", "text/plain", ""};

    // Static Frontend Files
    if (req.method == "GET" && (req.path == "/" || req.path == "/index.html")) {
        string html = readFile("frontend/index.html");
        if (html.empty()) return {404, "Not Found", "text/plain", "frontend/index.html not found. Run from project root."};
        return {200, "OK", "text/html", html};
    }
    if (req.method == "GET" && req.path == "/style.css") {
        string css = readFile("frontend/style.css");
        if (css.empty()) return {404, "Not Found", "text/plain", "frontend/style.css not found"};
        return {200, "OK", "text/css", css};
    }
    if (req.method == "GET" && req.path == "/app.js") {
        string js = readFile("frontend/app.js");
        if (js.empty()) return {404, "Not Found", "text/plain", "frontend/app.js not found"};
        return {200, "OK", "application/javascript", js};
    }

    // GET /api/restaurants
    if (req.method == "GET" && req.path == "/api/restaurants") {
        auto list = session.restaurants.allValues();
        sort(list.begin(), list.end(), [](const Restaurant& a, const Restaurant& b) { return a.id < b.id; });
        ostringstream oss;
        oss << "{\"bucketCount\":" << session.restaurants.bucketCount() << ",\"restaurants\":[";
        for (size_t i = 0; i < list.size(); ++i) {
            if (i > 0) oss << ",";
            oss << restaurantToJson(list[i], session.restaurants.hash(list[i].id), session.restaurants.chainSizeAt(list[i].id));
        }
        oss << "]}";
        return {200, "OK", "application/json", oss.str()};
    }

    // GET /api/restaurants/:id
    if (req.method == "GET" && req.path.rfind("/api/restaurants/", 0) == 0) {
        string id = req.path.substr(17);
        Restaurant* r = session.restaurants.get(id);
        if (!r) return {404, "Not Found", "application/json", "{\"error\":\"Restaurant not found\"}"};
        return {200, "OK", "application/json",
                restaurantToJson(*r, session.restaurants.hash(id), session.restaurants.chainSizeAt(id))};
    }

    // GET /api/menu
    if (req.method == "GET" && req.path == "/api/menu") {
        auto items = session.menu.allValues();
        sort(items.begin(), items.end(), [](const MenuItem& a, const MenuItem& b) { return a.id < b.id; });
        ostringstream oss;
        oss << "{\"bucketCount\":" << session.menu.bucketCount() << ",\"items\":[";
        for (size_t i = 0; i < items.size(); ++i) {
            if (i > 0) oss << ",";
            Restaurant* r = session.restaurants.get(items[i].restaurantId);
            string rName = r ? r->name : items[i].restaurantId;
            oss << menuItemToJson(items[i], session.menu.hash(items[i].id), session.menu.chainSizeAt(items[i].id), rName);
        }
        oss << "]}";
        return {200, "OK", "application/json", oss.str()};
    }

    // GET /api/menu/:id
    if (req.method == "GET" && req.path.rfind("/api/menu/", 0) == 0) {
        string id = req.path.substr(10);
        MenuItem* m = session.menu.get(id);
        if (!m) return {404, "Not Found", "application/json", "{\"error\":\"Menu item not found\"}"};
        Restaurant* r = session.restaurants.get(m->restaurantId);
        string rName = r ? r->name : m->restaurantId;
        return {200, "OK", "application/json",
                menuItemToJson(*m, session.menu.hash(id), session.menu.chainSizeAt(id), rName)};
    }

    // GET /api/cart
    if (req.method == "GET" && req.path == "/api/cart") {
        return {200, "OK", "application/json", cartStateToJson(session)};
    }

    // POST /api/cart/items
    if (req.method == "POST" && req.path == "/api/cart/items") {
        string itemId = extractJsonString(req.body, "itemId");
        int qty = extractJsonInt(req.body, "qty", 1);
        if (itemId.empty() || qty <= 0) {
            return {400, "Bad Request", "application/json", "{\"error\":\"Valid itemId and positive qty required\"}"};
        }
        MenuItem* m = session.menu.get(itemId);
        if (!m) return {404, "Not Found", "application/json", "{\"error\":\"Menu item not found\"}"};
        session.add(*m, qty);
        return {200, "OK", "application/json", cartStateToJson(session)};
    }

    // PUT /api/cart/items/:id
    if (req.method == "PUT" && req.path.rfind("/api/cart/items/", 0) == 0) {
        string id = req.path.substr(16);
        int qty = extractJsonInt(req.body, "qty", -1);
        MenuItem* m = session.menu.get(id);
        if (!m) return {404, "Not Found", "application/json", "{\"error\":\"Menu item not found\"}"};
        bool ok = (qty <= 0) ? session.remove(*m) : session.changeQty(*m, qty);
        if (!ok) return {400, "Bad Request", "application/json", "{\"error\":\"Item is not in cart\"}"};
        return {200, "OK", "application/json", cartStateToJson(session)};
    }

    // DELETE /api/cart/items/:id
    if (req.method == "DELETE" && req.path.rfind("/api/cart/items/", 0) == 0) {
        string id = req.path.substr(16);
        MenuItem* m = session.menu.get(id);
        if (!m) return {404, "Not Found", "application/json", "{\"error\":\"Menu item not found\"}"};
        if (!session.remove(*m)) return {404, "Not Found", "application/json", "{\"error\":\"Item not in cart\"}"};
        return {200, "OK", "application/json", cartStateToJson(session)};
    }

    // POST /api/cart/undo
    if (req.method == "POST" && req.path == "/api/cart/undo") {
        Action undone{};
        if (!session.undo(undone)) {
            return {400, "Bad Request", "application/json", "{\"error\":\"Stack is empty — nothing to undo\"}"};
        }
        ostringstream oss;
        oss << "{\"undone\":" << actionToJson(undone) << ",\"cart\":" << cartStateToJson(session) << "}";
        return {200, "OK", "application/json", oss.str()};
    }

    // POST /api/cart/clear
    if (req.method == "POST" && req.path == "/api/cart/clear") {
        session.clearCart();
        return {200, "OK", "application/json", cartStateToJson(session)};
    }

    // GET /api/orders
    if (req.method == "GET" && req.path == "/api/orders") {
        return {200, "OK", "application/json", ordersStateToJson(session)};
    }

    // POST /api/orders
    if (req.method == "POST" && req.path == "/api/orders") {
        int id = session.checkout();
        if (id == -1) return {400, "Bad Request", "application/json", "{\"error\":\"Cannot checkout an empty cart\"}"};
        Order* ord = session.orderRecords.get(to_string(id));
        ostringstream oss;
        oss << "{\"order\":" << orderToJson(*ord)
            << ",\"ordersState\":" << ordersStateToJson(session)
            << ",\"cart\":" << cartStateToJson(session) << "}";
        return {201, "Created", "application/json", oss.str()};
    }

    // POST /api/orders/advance
    if (req.method == "POST" && req.path == "/api/orders/advance") {
        int orderId = -1;
        Status newStatus = session.advanceFront(orderId);
        if (orderId == -1) return {400, "Bad Request", "application/json", "{\"error\":\"Order queue is empty\"}"};
        Order* ord = session.orderRecords.get(to_string(orderId));
        ostringstream oss;
        oss << "{\"advancedOrderId\":" << orderId
            << ",\"newStatus\":\"" << statusToString(newStatus)
            << "\",\"order\":" << orderToJson(*ord)
            << ",\"ordersState\":" << ordersStateToJson(session) << "}";
        return {200, "OK", "application/json", oss.str()};
    }

    // GET /api/orders/:id
    if (req.method == "GET" && req.path.rfind("/api/orders/", 0) == 0) {
        string id = req.path.substr(12);
        Order* ord = session.orderRecords.get(id);
        if (!ord) return {404, "Not Found", "application/json", "{\"error\":\"Order not found\"}"};
        ostringstream oss;
        oss << "{\"bucket\":" << session.orderRecords.hash(id)
            << ",\"chainLength\":" << session.orderRecords.chainSizeAt(id)
            << ",\"order\":" << orderToJson(*ord) << "}";
        return {200, "OK", "application/json", oss.str()};
    }

    return {404, "Not Found", "application/json", "{\"error\":\"Endpoint not found\"}"};
}

// ============================================================
// 10. MAIN SERVER LOOP
// ============================================================
int main() {
#ifdef _WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        cerr << "WSAStartup failed.\n";
        return 1;
    }
#else
    signal(SIGPIPE, SIG_IGN);
#endif

    SocketType serverFd = socket(AF_INET, SOCK_STREAM, 0);
    if (serverFd == INVALID_SOCKET) {
        cerr << "Failed to create socket.\n";
        return 1;
    }

    int opt = 1;
    setsockopt(serverFd, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&opt), sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(8080);

       if (::bind(serverFd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        cerr << "Port 8080 is already in use or unavailable.\n";
        CLOSE_SOCKET(serverFd);
        return 1;
    }

    if (listen(serverFd, 16) < 0) {
        cerr << "Failed to listen on port 8080.\n";
        CLOSE_SOCKET(serverFd);
        return 1;
    }

    OrderingSession session;
    cout << "====================================================\n";
    cout << " Online Food Ordering System (C + C++17 Backend)\n";
    cout << " Open in browser: http://localhost:8080\n";
    cout << " Press Ctrl+C to stop.\n";
    cout << "====================================================\n";

    while (true) {
        SocketType clientFd = accept(serverFd, nullptr, nullptr);
        if (clientFd == INVALID_SOCKET) continue;

        HttpRequest req;
        if (readHttpRequest(clientFd, req)) {
            HttpResponse res = handleRequest(req, session);
            sendHttpResponse(clientFd, res);
        }
        CLOSE_SOCKET(clientFd);
    }

    CLOSE_SOCKET(serverFd);
#ifdef _WIN32
    WSACleanup();
#endif
    return 0;
}
