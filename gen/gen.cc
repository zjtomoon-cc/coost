#include "co/def.h"
#include "co/str.h"
#include "co/fs.h"
#include "co/flag.h"
#include "co/cout.h"

DEF_bool(cpp, false, "generate code for C++");
DEF_bool(go, false, "generate code for golang"); 
 
void gen_cpp(
    const fastring& gen_file, const fastring& pkg, const fastring& serv, 
    const std::vector<fastring>& methods
) {
    fs::fstream fs(gen_file.c_str(), 'w');
    if (!fs) {
        COUT << "cannot open file: " << gen_file;
        exit(0);
    }

    // service name: pkg.serv or serv
    fastring serv_name(pkg);
    if (!serv_name.empty()) serv_name.append('.');
    serv_name.append(serv);

    // packages.  "xx.yy" -> ["xx", "yy"]
    auto pkgs = str::split(pkg, '.');

    // includes
    do {
        fs << "// Autogenerated.\n"
           << "// DO NOT EDIT. All changes will be undone.\n"
           << "#pragma once\n\n"
           << "#include \"co/rpc.h\"\n\n";
    } while (0);

    // namespaces
    for (size_t i = 0; i < pkgs.size(); ++i) {
        fs << "namespace " << pkgs[i] << " {\n";
    }
    if (!pkgs.empty()) fs << "\n";

    // class for service
    fs << "class " << serv << " : public rpc::Service {\n";
    fs << "  public:\n";
    fs << fastring(' ', 4) << "typedef std::function<void(Json&, Json&)> Fun;\n\n";

    do {
        fs << fastring(' ', 4) << serv << "() {\n";
        fs << fastring(' ', 8) << "using std::placeholders::_1;\n";
        fs << fastring(' ', 8) << "using std::placeholders::_2;\n";
        for (size_t i = 0; i < methods.size(); ++i) {
            fs << fastring(' ', 8) << "_methods[\"" << serv << '.' << methods[i] << "\"] = "
               << "std::bind(&" << serv << "::" << methods[i] << ", this, _1, _2);\n";
        }
        fs << fastring(' ', 4) << "}\n\n";

        fs << fastring(' ', 4) << "virtual ~" << serv << "() {}\n\n";
    } while (0);

    // virtual const char* name() const
    fs << fastring(' ', 4) << "virtual const char* name() const {\n"
       << fastring(' ', 8) << "return \"" << serv << "\";\n"
       << fastring(' ', 4) << "}\n\n";

    fs << fastring(' ', 4) << "virtual const co::map<const char*, Fun>& methods() const {\n"
       << fastring(' ', 8) << "return _methods;\n"
       << fastring(' ', 4) << "}\n\n";

    // virtual void xxx(Json& req, Json& res)
    for (size_t i = 0; i < methods.size(); ++i) {
        fs << fastring(' ', 4) << "virtual void " << methods[i] << "(Json& req, Json& res) = 0;\n\n";
    }

    fs << "  private:\n";
    fs << "    co::map<const char*, Fun> _methods;\n";
    fs << "};\n";

    if (!pkgs.empty()) fs << '\n';

    for (size_t i = 0; i < pkgs.size(); ++i) {
        fs << "} // " << pkgs[i] << "\n";
    }

    fs.flush();
    COUT << "generate " << gen_file << " success";
}

// todo: support golang
void gen_go(
    const fastring& gen_file, const fastring& pkg, const fastring& serv, 
    const std::vector<fastring>& methods
) {
}

void parse(const char* path) {
    fs::file f;
    if (!f.open(path, 'r')) {
        COUT << "failed to open file: " << path;
        exit(-1);
    }

    const char* b = strrchr(path, '/');
    if (b == 0) b = strrchr(path, '\\');
    b == 0 ? (b = path) : ++b;
    const char* e = strrchr(path, '.');

    if (e == 0 || e <= b) {
        COUT << "invalid proto file name: " << path;
        exit(-1);
    }

    fastring gen_file(fastring(b, e - b) + ".h");
    fastring pkg;
    fastring serv;
    std::vector<fastring> methods;

    auto s = f.read(fs::fsize(path));
    char c = '\n';
    if (!strchr(s.c_str(), '\n') && strchr(s.c_str(), '\r')) c = '\r';

    auto l = str::split(s.c_str(), c);

    for (size_t i = 0; i < l.size(); ++i) {
        auto x = str::strip(l[i]);
        if (x.empty()) continue;
        if (x.starts_with("//")) continue;

        if (x.starts_with("package ")) {
            if (!pkg.empty()) {
                COUT << "find multiple package name in file: " << path;
                exit(-1);
            }

            const char* p = strstr(x.c_str(), "//");
            if (p) x.resize(p - x.data());
            pkg = x.c_str() + 8;
            pkg = str::strip(pkg);
            continue;
        }

        if (x.starts_with("service ")) {
            if (!serv.empty()) {
                COUT << "find multiple service in file: " << path;
                exit(-1);
            }

            const char* p = strstr(x.c_str(), "//");
            if (p) x.resize(p - x.data());
            serv = x.c_str() + 8;
            serv = str::strip(serv, " \t\r\n{");

            for (size_t k = i + 1; k < l.size(); ++k) {
                const char* p = strstr(l[k].c_str(), "//");
                if (p) l[k].resize(p - l[k].data());

                if (l[k].find('}') != l[k].npos) {
                    auto m = str::strip(l[k], " \t\r\n,;{}");
                    if (!m.empty()) methods.push_back(m);
                    if (methods.empty()) {
                        COUT << "no method found in service: " << serv;
                        exit(-1);
                    }

                    if (!FLG_cpp && !FLG_go) FLG_cpp = true; // gen cpp by default
                    if (FLG_cpp) gen_cpp(gen_file, pkg, serv, methods);
                    if (FLG_go) gen_go(gen_file, pkg, serv, methods);
                    return;
                } else {
                    auto m = str::strip(l[k], " \t\r\n,;{");
                    if (!m.empty()) methods.push_back(m);
                }
            }

            COUT << "ending '}' not found for service: " << serv;
            exit(-1);
        }
    }
}

int main(int argc, char** argv) {
    auto v = flag::init(argc, argv);
    if (v.empty()) {
        COUT << "usage: gen xx.proto";
        return 0;
    }

    for (size_t i = 0; i < v.size(); ++i) {
        parse(v[i].c_str());
    }

    return 0;
}
