#include "kernel.h"
#include "connection.h"
#include "devices/gpio.h"

const char * mod_str(const uint8_t & mod)
{
    switch (mod)
    {
        case r__: return "r--";
        case _w_: return "-w-";
        case __x: return "--x";
        case rw_: return "rw-";
        case rwx: return "ker";
        default: return "---";
    }
}

uint8_t gen_tid()
{
    static uint8_t g_tid = 0;
    return g_tid++;
}

//
// String Processing
//

template<>
uint32_t Kernel::parse(const str& s)
{
    return std::max(0UL, stoul(s, 0, 0));
}

template<>
uint8_t Kernel::parse(const str& s)
{
    return std::max(0UL, std::min(stoul(s, 0, 0), 255UL));
}

template<>
bool Kernel::parse(const str& s)
{
    if (s.size() != 1 || s[0] < '0' || s[0] > '1' ) throw std::invalid_argument("not 0 or 1");
    return s[0] == '1';
}

template<>
tAct Kernel::parse(const str& s)
{
    if (s == "none") return nullptr;

    auto it = m_nodes.find(s);
    if (it == m_nodes.end()) throw std::invalid_argument("invalid path");

    const Node& n = it->second;
    if (!is<tAct>(n)) throw std::invalid_argument("not an action");

    return get<tAct>(n);
}

template<typename U>
U* Kernel::parse_device(const str& s)
{
    if (s == "none") return nullptr;

    auto it = m_nodes.find(s);
    if (it == m_nodes.end()) throw std::invalid_argument("invalid path");

    const Node& n = it->second;
    if (!is<Device*>(n)) throw std::invalid_argument("not a device");

    U* c = dynamic_cast<U*>(get<Device*>(n));
    if (!c) throw std::invalid_argument((str("not a ") + U::ClassName).c_str());

    return c;
}

void Kernel::tokenize(vec<char>& v, vec<str>& args)
{
//    v.push_back('\0');
    args.clear();
    char* s = strtok(v.data()," \n");
    while(s) {
        args.emplace_back(s);
        s = strtok(0, " \n");
    }
}

str Kernel::autocomplete(const str& s, vec<str>& options, vec<int>& counts, bool group)
{
    options.clear();
    counts.clear();

    auto it = m_nodes.lower_bound(s);
    if (it == m_nodes.end()) return "";

    str ref = it->first;
    int l = ref.size();

    if (ref.substr(0, s.size()) != s) return "";
    options.push_back(ref);
    counts.push_back(1);

    str last_option = ref;

    while(++it != m_nodes.end())
    {
        int cmp = 0;
        const str& s2 = it->first;

        while (cmp < l && cmp < s2.size())
        {
            if (ref[cmp] == s2[cmp]) cmp++;
            else break;
        }

        if (cmp < s.size()) break; // doesn't match with s prefix
        l = std::min(l, cmp);

        if (group)
        {
            int n = s.size();
            while(n++ < s2.size()) if (s2[n-1] == '.') break;

            if (!strncmp(last_option.c_str(), s2.c_str(), n)) {
                counts.back()++;
                continue;
            }
            else {
                options.push_back(s2.substr(0, n));
                counts.push_back(1);
                last_option = s2;
            }
        } else {
            options.push_back(s2);
            counts.push_back(1);
        }
    }

    return ref.substr(s.size(), l - s.size());
}

//
// Kernel
//

Kernel::Kernel() : Device("kernel")
{
    soft_reset();
}

void Kernel::soft_reset()
{
    Device::soft_reset();

    m_id = "";
    m_permissions = 0;
    loop_debug = nullptr;
    m_current_connection = 0;

    m_nodes.clear();
    m_connections.clear();

    Group<bool*>::m_pointers.clear();
    Group<str*>::m_pointers.clear();
    Group<uint8_t*>::m_pointers.clear();
    Group<uint32_t*>::m_pointers.clear();
    Group<Device*>::m_pointers.clear();
    Group<GPIO**>::m_pointers.clear();
    Group<Connection**>::m_pointers.clear();
    Group<tApp>::m_pointers.clear();
    Group<tAct>::m_pointers.clear();
    Group<tAct*>::m_pointers.clear();
}

void Kernel::add_to(Kernel& k, const str& path)
{
    k.add(&m_id, path + s_"id", rw_);
    k.add(&loop_debug, path + s_"debug", rw_);

    k.add(tApp(std::bind(&Kernel::login, this, _1, _2, _3)), "login", rwx, "<user>", "Login as user or root");
    k.add(tApp(std::bind(&Kernel::ls, this, _1, _2, _3)), "ls", __x, "[-ltmvda] [path]", "List commands");
    k.add(tApp(std::bind(&Kernel::man, this, _1, _2, _3)), "man", __x, "<path>", "Command manual");
}

void Kernel::init(const str& id, uint8_t permissions)
{
    soft_reset();

    m_id = id;
    m_permissions = permissions;

    m_initialized = true;

    add_device(this, "ker");
}

void Kernel::add_device(Device* device, str path)
{
    if(!m_initialized) throw "Kernel: not initialized";
    if (!device || path == "") throw "Kernel: Invalid argument(s)";

    add(device, path, r__);
//    add(&(device->m_initialized), path + s_"inited", r__);

    device->add_to(*this, path);
    device->m_path = path;

    Connection* c = dynamic_cast<Connection*>(device);
    if (c)
    {
        m_connections.push_back(c);
        m_current_connection = 0;

        if (m_permissions)
            c->m_autocomplete = std::bind(&Kernel::autocomplete, this, _1, _2, _3, _4);
    }
}

void Kernel::validate()
{
    if(!m_initialized) throw "Kernel: not initialized";

    for (auto d : Group<Device*>::m_pointers)
        if (!d->initialized()) throw (d->m_path + ": not initialized").c_str();
}

void Kernel::run(tAct setup, tAct loop, Connection* term)
{
    try
    {
        if (setup) setup();

        aos::start();
        validate();

        while(aos::running())
        {
            serve();

            if (loop) loop();

            aos::serve();
            aos::sleep(10 * us); // Saves 5 mA of power consumption
        }

        if (term) if (term->initialized()) term->print(GREEN_"\r\nDone!"_RESETn)->flush();
        aos::delay(10 * ms);  // wait to deliver the last message

        aos::hibernate();
//        aos::halt();
    }
    catch (const char* ex)
    {
        if (term) if (term->initialized()) term->printf(RED_"\r\n%s"_RESETn, ex)->flush();
                  else aos::hard_reset(); // too bad of a fault
    }
    catch (std::exception& ex)
    {
        if (term) if (term->initialized()) term->printf(RED_"\r\n%s"_RESETn, ex.what())->flush();
                  else aos::hard_reset(); // too bad of a fault
    }

    if (term) if (term->initialized()) term->println("Rebooting...")->flush();

    volatile uint32_t wait = 1000 * 1000;
    while(wait) wait--; // wait to deliver the last messages

    aos::stop();
    aos::soft_reset();
}

void Kernel::serve()
{
    if(!m_initialized) throw "Kernel: not initialized";
    if (loop_debug) loop_debug->set();

    if (m_current_connection < m_connections.size())
    {
        Connection* cin = m_connections.at(m_current_connection);
        Connection* cout = cin->cout;
        Connection* debug = cin->debug;

        bool flush = false;

        while (!cin->rx_empty())
        {
            vec<char> v;
            cin->read(v, true); // null terminated

            if (cin->pipe && v.at(0) != '>')
            {
                cin->pipe->print(v.data(), v.size() - 1)->flush();
                continue;
            }

            flush = true;

            vec<str> args;
            tokenize(v, args);

            if (args.size() == 0)
            {
                if (debug) debug->print("Empty command\n");
                continue;
            }

            str cmd = args.front();
            args.erase(args.begin());

            if (cmd[0] == '>')
            {
                if (args.size() == 0)
                {
                    if (debug) debug->print("Empty command\n");
                    continue;
                }

                if (cmd.size() == 1)
                {
                    cmd = args.front();
                    args.erase(args.begin());
                }
                else
                {
                    cmd = cmd.substr(1);
                    Connection* c = nullptr;
                    for (auto con : m_connections) if (con->m_path == cmd) c = con;

                    if (!c) { if (cout) cout->printf(YELLOW_"%s: connection not found"_RESETn, cmd); }
                    else {
                        str s = "";
                        for (auto& arg : args) s += arg + " "; // args not empty (see above)
                        c->print(s.c_str(), s.size() - 1)->flush();
                    }
                    continue;
                }
            }

            if (cmd[0] == ':')
            {
                if (cmd.size() == 1)
                {
                    if (cout) cout->print(YELLOW_"Specify id!"_RESETn);
                    continue;
                }

                if (cmd.substr(1) != m_id) continue;

                if (args.size() == 0)
                {
                    if (debug) debug->print("Empty command\n");
                    continue;
                }

                cmd = args.front();
                args.erase(args.begin());
            }

            if (cmd[0] == '!')
            {
                if (cmd.size() == 1)
                {
                    if (debug) debug->print("Empty command\n");
                    continue;
                }

                if (cout) cout->autoPost(false)->print(cmd.c_str() + 1)->print(": ")->autoPost(true);
                cmd = cmd.substr(1);
            }

            auto it = m_nodes.find(cmd);

            if (it == m_nodes.end())
            {
                if (cout) cout->printf(YELLOW_"%s: cmd not found"_RESETn, cmd);
                continue;
            }

            const Node& n = it->second;

            if (is<tApp>(n))
            {
                if (!(n.mod & __x & m_permissions) && n.mod != rwx)
                {
                    cout->printf(MAGENTA_"%s: access denied"_RESETn, cmd);
                    continue;
                }

                int ret = get<tApp>(n)(args, cout, debug);

                if (ret && cout)
                {
                    if (ret & APP_ERROR) cout->printf(MAGENTA_"%s: error code %d"_RESETn, cmd, ret & ~APP_FLAGS_MASK);
                    else if (ret & APP_TOO_MANY_ARGS) cout->printf(YELLOW_"%s: too many arguments"_RESETn, cmd);
                    else if (ret & APP_INVALID_ARGS) cout->printf(YELLOW_"%s: invalid argument(s)"_RESETn, cmd);
                    else if (ret & APP_TOO_FEW_ARGS) cout->printf(YELLOW_"%s: too few arguments"_RESETn, cmd);
                    else cout->printf(MAGENTA_"%s: returned %d"_RESETn, cmd, ret);
                }
                continue;
            }

            if (is<tAct>(n))
            {
                if (!(n.mod & __x & m_permissions))
                {
                    cout->printf(MAGENTA_"%s: access denied"_RESETn, cmd);
                    continue;
                }

                get<tAct>(n)();
                continue;
            }

            if (args.size() == 0 && cout)
            {
                if (!(n.mod & r__ & m_permissions))
                {
                    cout->printf(MAGENTA_"%s: access denied"_RESETn, cmd);
                    continue;
                }

                if (is<uint8_t*>(n)) cout->printf("%d\n", as<uint8_t>(n));
                else if (is<uint32_t*>(n)) cout->printf("%d\n", as<uint32_t>(n));
                else if (is<bool*>(n)) cout->printf("%d\n", as<bool>(n));
                else if (is<str*>(n)) cout->printf("%s\n", as<str>(n));
                else if (is<Device*>(n)) cout->printf("%s\n", as<Device>(n).m_type);
                else if (is<Connection**>(n)) cout->printf("%s\n", as<Connection*>(n) ? as<Connection*>(n)->m_path : "none");
                else if (is<GPIO**>(n)) cout->printf("%s\n", as<GPIO*>(n) ? as<GPIO*>(n)->m_path : "none");
                else if (is<tAct*>(n)) cout->printf("%s\n", as<tAct>(n) ? "set" : "none");
//                else if (is<tGet<uint32_t> >(n)) cout->printf("%d\n", find<tGet<uint32_t> >(n)());
//                else if (is<tGet<bool> >(n)) cout->printf("%d\n", find<tGet<bool> >(n)());
                else cout->printf(YELLOW_"%s: too few arguments"_RESETn, cmd);
            }
            else if (args.size() > 1 && cout)
            {
                cout->printf(YELLOW_"%s: too many arguments"_RESETn, cmd);
            }
            else
            {
                if (!(n.mod & _w_ & m_permissions))
                {
                    cout->printf(MAGENTA_"%s: access denied"_RESETn, cmd);
                    continue;
                }
                try {
                    if (is<uint8_t*>(n)) as<uint8_t>(n) = parse<uint8_t>(args.front());
                    else if (is<uint32_t*>(n)) as<uint32_t>(n) = parse<uint32_t>(args.front());
                    else if (is<bool*>(n)) as<bool>(n) = parse<bool>(args.front());
                    else if (is<str*>(n)) as<str>(n) = args.front();
                    else if (is<Device*>(n)) throw std::invalid_argument("read only");
//                    else if (is<tAct>(n)) throw std::invalid_argument("exec only");
//                    else if (is<tApp>(n)) throw std::invalid_argument("exec only");
                    else if (is<Connection**>(n)) as<Connection*>(n) = parse_device<Connection>(args.front());
                    else if (is<GPIO**>(n)) as<GPIO*>(n) = parse_device<GPIO>(args.front());
                    else if (is<tAct*>(n)) as<tAct>(n) = parse<tAct>(args.front());
//                    else if (is<tSet<uint32_t> >(n)) find<tSet<uint32_t> >(n)(parse<uint32_t>(args.front()));
//                    else if (is<tSet<bool> >(n)) find<tSet<bool> >(n)(parse<bool>(args.front()));
                    else cout->printf(MAGENTA_"%s: type not found"_RESETn, cmd);
                }
                catch (std::exception& ex)
                {
                    if (cout) cout->printf(MAGENTA_"%s: %s"_RESETn, args.front(), ex.what());
                }
            }
        }

        if (flush)
        {
            if (cout) cout->flush();
            if (debug) debug->flush();
        }

        cin->flush(); // MCU initiated messages
        m_current_connection = (m_current_connection + 1) % m_connections.size();
    }
    if (loop_debug) loop_debug->clear();
}

//
// Default Applications
//

int Kernel::login(const vec<str>& args, Connection* cout, Connection* debug)
{
    if (args.size() == 0) return APP_TOO_FEW_ARGS;
    if (args.size() > 1) return APP_TOO_MANY_ARGS;

    if (args.front() == "user") m_permissions = r__ | __x;
    else if (args.front() == "root") m_permissions = rw_ | __x;
    else if (cout) cout->print(MAGENTA_"invalid user name"_RESETn);

    if (m_permissions)
        for(auto& c : m_connections)
            c->m_autocomplete = std::bind(&Kernel::autocomplete, this, _1, _2, _3, _4);

    return APP_DONE;
}

int Kernel::ls(const vec<str>& args, Connection* cout, Connection* debug)
{
    if (!cout)
    {
        if (debug) debug->print(YELLOW_"ls: require cout"_RESETn);
        return APP_DONE;
    }

    bool type = false;
    bool mod = false;
    bool value = false;
    bool description = false;
    bool list = false;
    str s = "";

    if (args.size())
    {
        str f = args.front();

        if (f[0] == '-')
        {
            if (f.find('l') != str::npos) list = true;
            if (f.find('t') != str::npos) type = true;
            if (f.find('m') != str::npos) mod = true;
            if (f.find('v') != str::npos) value = true;
            if (f.find('d') != str::npos) description = true;
            if (f.find('a') != str::npos) type = mod = value = true;
        }
        else if (args.size() == 1) s = f;
        else return APP_INVALID_ARGS;

        if (args.size() > 1)
        {
            if (args.size() > 2) return APP_TOO_MANY_ARGS;
            else s = args.back();
        }
    }

    cout->autoPost(false);
    while(!cout->tx_ready()) aos::serve();

    cout->printf("%s%s%s%s\n", type ? "Type " : "", mod ? "Mod " : "", "Path [Usage]  ", value ? "\t: Value" : "");
    cout->post();

    vec<str> lines;
    vec<int> counts;
    str completed = s;

    if (!list) completed = autocomplete(s, lines, counts, true);

    str p = "";
    const Node* n = nullptr;
    auto lit = lines.begin();
    auto cit = counts.begin();
    auto nit = m_nodes.begin();

    while(list ? nit != m_nodes.end() : lit != lines.end())
    {
        if (list)
        {
            p = nit->first;
            n = &(nit->second);
            nit++;
        }
        else
        {
            p = *lit;
            n = p.back() == '.' ? nullptr : &find(p);
            lit++;
            cit++;
        }

        if (list && s != p.substr(0, s.size())) continue;

        str p2 = p;
        if (n) if (is<tApp>(*n) || is<tAct>(*n)) p2 += (n->usage ? str(" ") + str(n->usage) : "");
        if (value) while (p2.size() < 14) p2 += " ";

        while(!cout->tx_ready()) aos::serve();

        if (type)
        {
            str t = " ???";
            if (!n) t = " dir";
            else {
                if (is<bool*>(*n)) t = "bool";
                if (is<str*>(*n)) t = " str";
                if (is<uint8_t*>(*n)) t = "byte";
                if (is<uint32_t*>(*n)) t = "word";
                if (is<Device*>(*n)) t = " dev";
                if (is<GPIO**>(*n)) t = "pin*";
                if (is<Connection**>(*n)) t = "con*";
                if (is<tApp>(*n)) t = " app";
                if (is<tAct>(*n)) t = " act";
                if (is<tAct*>(*n)) t = "act*";
            }
            cout->print(t);

            cout->print(" ");
        }

        if (mod) cout->printf("%s ", n ? mod_str(n->mod) : "---");

        cout->print(p2);

        if (value)
        {
            if (!n) cout->printf("\t: %d", *(cit - 1));
            else {
                if (is<bool*>(*n)) cout->printf("\t: %d", as<bool>(*n));
                if (is<str*>(*n)) cout->printf("\t: %s", as<str>(*n));
                if (is<uint8_t*>(*n)) cout->printf("\t: %d", as<uint8_t>(*n));
                if (is<uint32_t*>(*n)) cout->printf("\t: %d", as<uint32_t>(*n));
                if (is<Device*>(*n)) cout->printf("\t: %s", as<Device>(*n).m_type);
                if (is<GPIO**>(*n)) cout->printf("\t: %s", as<GPIO*>(*n) ? as<GPIO*>(*n)->m_path : "none");
                if (is<Connection**>(*n)) cout->printf("\t: %s", as<Connection*>(*n) ? as<Connection*>(*n)->m_path : "none");
                if (is<tAct*>(*n)) cout->printf("\t: %s", as<tAct>(*n) ? "set" : "none");
            }
        }

        if (description)
        {
            if (!n) cout->printf("\t- Directory");
            else if (n->description) cout->printf("\t- %s", n->description);
        }

        cout->print("\n");
        cout->flush();
    }
    cout->autoPost(true);

    return APP_DONE;
}

int Kernel::man(const vec<str>& args, Connection* cout, Connection* debug)
{
    if (args.size() == 0) return APP_TOO_FEW_ARGS;
    if (args.size() > 1) return APP_TOO_MANY_ARGS;

    str path = args.front();
    if (!exists(path)) return APP_INVALID_ARGS;

    auto n = find(path);
    if (cout) cout->printf("%s %s%s\n", path, n.usage ? n.usage : "",
                                              n.description ? str(" - ") + str(n.description) : "");
    return APP_DONE;
}
