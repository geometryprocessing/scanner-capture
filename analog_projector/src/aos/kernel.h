#ifndef KERNEL_H
#define KERNEL_H

#include <map>

#include "device.h"

class GPIO;
class Connection;

using namespace std::placeholders;
using tApp = std::function<int(const vec<str>&, Connection*, Connection*)>;

template <typename T> using tGet    = std::function<T()>;
template <typename T> using tSet    = std::function<void(T)>;
template <typename T> using tParse  = std::function<T(const str&)>;
template <typename T> using tFormat = std::function<str(const T&)>;

extern const char * mod_str(const uint8_t & mod);

#define APP_DONE            0x00000000
#define APP_TOO_FEW_ARGS    0x01000000
#define APP_TOO_MANY_ARGS   0x02000000
#define APP_INVALID_ARGS    0x04000000
#define APP_ERROR           0x08000000
#define APP_FLAGS_MASK      0x0F000000

#define s_      "."

#define r__     0b100
#define _w_     0b010
#define __x     0b101
#define rw_     0b110
#define rwx     0b111

extern uint8_t gen_tid();

template <typename T>
class Group
{
public:
    Group() : m_tid(gen_tid()) {}

protected:
    const uint8_t m_tid;
    vec<T> m_pointers;
};

class Kernel : public Group<bool*>, public Group<str*>, public Device,
               public Group<uint8_t*>, public Group<uint32_t*>,
//               public Group<tGet<uint8_t> >, public Group<tSet<uint8_t> >,
//               public Group<tParse<uint8_t> >, public Group<tFormat<uint8_t> >,
//               public Group<tGet<uint32_t> >, public Group<tSet<uint32_t> >,
//               public Group<tParse<uint32_t> >, public Group<tFormat<uint32_t> >,
               public Group<Device*>, public Group<GPIO**>, public Group<Connection**>,
               public Group<tApp>, public Group<tAct>, public Group<tAct*>
{
public:
    Kernel();

    void init(const str& id, uint8_t permissions);
    void soft_reset() override;

    void add_to(Kernel& k, const str& path) override;

    void validate(); // Checks for uninitialized devices
    void serve();  // Processes connection messages
    void run(tAct setup, tAct loop, Connection* term); // Exits on exception

    GPIO* loop_debug;

    struct Node
    {
        uint8_t tid, pid, mod;
        const char *usage, *description;

        Node(uint8_t tid = 0, uint8_t pid = 0, uint8_t mod = 0,
             const char * usage = nullptr, const char * description = nullptr) :
             tid(tid), pid(pid), mod(mod), usage(usage), description(description) {}
    };

    template<typename U>
    void add(U node, const str& path, uint8_t mod, const char * usage = nullptr,
                                                   const char * description = nullptr)
    {
        if(!m_initialized) throw "Kernel: not initialized";
        if (Group<U>::m_pointers.size() == 256) throw "Kernel: pid overflow";
        if (exists(path)) throw "Kernel: path duplicate";

        Group<U>::m_pointers.push_back(node);
        m_nodes[path] = Node(Group<U>::m_tid,
                             Group<U>::m_pointers.size()-1, mod, usage, description);
    }

    void add_device(Device* d, str path);

    inline bool exists(const str& path) { return m_nodes.find(path) != m_nodes.end(); }
    inline const Node& find(const str& path) { return m_nodes.at(path); }

    template<typename U>
    inline bool is(const Node& node) { return node.tid == Group<U>::m_tid; }

    template<typename U>
    inline U& get(const Node& node) { return Group<U>::m_pointers.at(node.pid); }

    template<typename U>
    inline U& as(const Node& node) { return *(Group<U*>::m_pointers.at(node.pid)); }

    template<typename U>
    U parse(const str& s);

    template<typename U>
    U* parse_device(const str& s);

    void tokenize(vec<char>& v, vec<str>& args);
    str autocomplete(const str& s, vec<str>& options, vec<int>& counts, bool group);

    int login(const vec<str>& args, Connection* cout, Connection* debug);
    int ls(const vec<str>& args, Connection* cout, Connection* debug);
    int man(const vec<str>& args, Connection* cout, Connection* debug);

private:
    str m_id;
    uint8_t m_permissions;
    uint8_t m_current_connection;

    std::map<str, Node> m_nodes;
    vec<Connection*> m_connections;
};

#endif /* KERNEL_H */
