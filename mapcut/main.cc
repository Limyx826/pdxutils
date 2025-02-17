#include <ck2.h>
#include <ck2/filesystem.h>

#include "region_file.h"

#include <cstdio>
#include <cerrno>
#include <cstring>
#include <string>
#include <unordered_map>
#include <fstream>
#include <iomanip>
#include <algorithm>


using namespace ck2::fs;
typedef unsigned int uint;


/* TODO: use Boost::ProgramOptions (or just a config file), and end this nonsense */

const path VROOT_DIR("/var/local/vanilla-ck2");
const path REPO_ROOT_DIR("/home/hiphub/git");
const path ROOT_DIR = REPO_ROOT_DIR / "SWMH-BETA/SWMH";
const path OUT_ROOT_DIR = REPO_ROOT_DIR / "MiniSWMH/MiniSWMH";
const path EMF_ROOT_DIR = REPO_ROOT_DIR / "EMF/EMF";
const path EMF_SWMH_ROOT_DIR = REPO_ROOT_DIR / "EMF/EMF+SWMH";
const path EMF_OUT_ROOT_DIR = REPO_ROOT_DIR / "EMF/EMF+MiniSWMH";

/*
const path VROOT_DIR("/home/ziji/vanilla");
const path ROOT_DIR("/cygdrive/c/git/SWMH-BETA/SWMH");
const path OUT_ROOT_DIR("/cygdrive/c/git/MiniSWMH/MiniSWMH");
const path EMF_ROOT_DIR("/cygdrive/c/git/EMF/EMF");
const path EMF_SWMH_ROOT_DIR("/cygdrive/c/git/EMF/EMF+SWMH");
const path EMF_OUT_ROOT_DIR("/cygdrive/c/git/EMF/EMF+MiniSWMH");
*/

const path TITLES_FILE("swmh_landed_titles.txt");
const path HOLYSITES_FILE("z_holy_sites.txt");

const int TAB_WIDTH = 4;

typedef std::vector<std::string> strvec_t;
typedef std::unordered_map<std::string, uint> str2id_map_t;


const ck2::block* find_title(const char* title, const ck2::block* p_root);
void find_titles_under(const ck2::block*, strvec_t& out);
void fill_county_to_id_map(const ck2::VFS&, const ck2::DefaultMap&, const ck2::DefinitionsTable&, str2id_map_t& out);
void blank_title_history(const ck2::VFS&, const strvec_t&);


class lt_printer {
    std::ofstream os;
    strvec_t      top_titles;
    uint          indent;
    bool          in_code_block;

    void print(const ck2::block&);
    void print(const ck2::list&);
    void print(const ck2::statement&);
    void print(const ck2::object&);

public:
    lt_printer(const fs::path& out_path, const strvec_t& top_titles, const ck2::block* p_root_block);
};


struct {
    uint n_counties_before;
    uint n_counties_cut;
    uint n_titles_cut;
    uint n_title_hist_blanked;
    uint n_adjacencies_cut;
} g_stats;


int main(int argc, char** argv) {

    memset(&g_stats, 0, sizeof(g_stats));

    if (argc < 2) {
        fprintf(stderr, "USAGE:\n  %s TITLE [TITLE ...]\n\nTITLE: top de jure title to remove (if plural, should not overlap)\n", argv[0]);
        return 1;
    }

    bool emf = false;
    strvec_t top_titles;

    for (int i = 1; i < argc; ++i) {
        const char* t = argv[i];

        if (strcmp(t, "--emf") == 0) {
            emf = true;
            continue;
        }

        assert( ck2::looks_like_title(t) );
        assert( ck2::title_tier(t) >= ck2::TIER_COUNT );
        top_titles.emplace_back(t);
    }

    try {
        ck2::VFS vfs{ VROOT_DIR };
        vfs.push_mod_path(ROOT_DIR);

        if (emf) {
            vfs.push_mod_path(EMF_ROOT_DIR);
            vfs.push_mod_path(EMF_SWMH_ROOT_DIR);
        }

        ck2::DefaultMap dm(vfs);
        ck2::DefinitionsTable def_tbl(vfs, dm);
        region_file geo_regions( vfs["map" / dm.geo_region_path()] );
        region_file island_regions( vfs["map" / dm.island_region_path()] );
        ck2::AdjacenciesFile adjacencies(vfs, dm);

        str2id_map_t county_to_id_map;
        fill_county_to_id_map(vfs, dm, def_tbl, county_to_id_map);
        g_stats.n_counties_before = county_to_id_map.size();

        ck2::parser parse( vfs["common/landed_titles" / TITLES_FILE] );
        strvec_t del_titles;

        for (const auto& top_title : top_titles) {
            const ck2::block* p_top_title_block = find_title(top_title.c_str(), parse.root_block());

            if (p_top_title_block == nullptr)
                throw ck2::Error("Top de jure title '{}' not found!", top_title);

            del_titles.emplace_back(top_title);
            find_titles_under(p_top_title_block, del_titles);
        }

        g_stats.n_titles_cut = del_titles.size();

        /* for every deleted county title, convert its associated province into
           wasteland */

        for (const auto& t : del_titles) {
            uint tier = ck2::title_tier(t.c_str());

            if (tier == ck2::TIER_DUKE) {
                /* delete duchy titles from the geographical region file */
                geo_regions.delete_duchy(t);
                island_regions.delete_duchy(t);
                continue;
            }

            if (tier != ck2::TIER_COUNT)
                continue;

            auto i = county_to_id_map.find(t);

            if (i == county_to_id_map.end())
                throw ck2::Error("County not assigned in province history: {}", t);

            uint id = i->second;

            /* delete any trace of the county from the geographical region file or island region file */

            geo_regions.delete_county(t);
            geo_regions.delete_province(id);
            island_regions.delete_county(t);
            island_regions.delete_province(id);

            /* remove any special adjacencies that are from or to the deleted province (through is permitted) */

            for (auto&& adj : adjacencies) {
                if (adj.deleted) continue;
                if (adj.from == id || adj.to == id) {
                    adj.deleted = true;
                    ++g_stats.n_adjacencies_cut;
                }
            }

            /* blank the province "name" in definitions to turn it into a wasteland */
            def_tbl[id].name = "";

            ++g_stats.n_counties_cut;
        }

    /* parse z_holy_sites.txt */
        ck2::parser hs_parse( vfs["common/landed_titles" / HOLYSITES_FILE] );

        path out_root = (emf) ? EMF_OUT_ROOT_DIR : OUT_ROOT_DIR;
        path out_map_root(out_root / "map");
        create_directories(out_map_root);

        /* write new region file (already just ensured its directories were created) */
        geo_regions.write(out_map_root / dm.geo_region_path());

        if (!emf) {
            /* write special adjacencies file */
            adjacencies.write(out_map_root / dm.adjacencies_path());

            /* write island_region file */
            island_regions.write(out_map_root / dm.island_region_path());

            /* write definition file */
            def_tbl.write(out_map_root / dm.definitions_path());

            /* rewrite landed_titles */
            path out_lt_path(out_root / "common" / "landed_titles");
            create_directories(out_lt_path);
            lt_printer( out_lt_path / TITLES_FILE, top_titles, parse.root_block() );

            /* rewrite holy_sites */
            lt_printer( out_lt_path / HOLYSITES_FILE, del_titles, hs_parse.root_block() );

            /* blank as much title history as necessary */
            blank_title_history(vfs, del_titles);
        }

        printf("Counties before cut:     %u\n", g_stats.n_counties_before);
        printf("Counties cut:            %u\n", g_stats.n_counties_cut);
        printf("Titles cut:              %u\n", g_stats.n_titles_cut);
        printf("Blanked title histories: %u\n", g_stats.n_title_hist_blanked);
        printf("Special adjacencies cut: %u\n", g_stats.n_adjacencies_cut);
    }
    catch (std::exception& e) {
        fprintf(stderr, "fatal: %s\n", e.what());
        return 1;
    }

    return 0;
}


const ck2::block* find_title(const char* top_title, const ck2::block* p_root) {

    uint top_title_tier = ck2::title_tier(top_title);

    for (const auto& s : *p_root) {
        if ( !(s.key().is_string() && ck2::looks_like_title( s.key().as_string() )) )
            continue;

        const char* t = s.key().as_string();
        assert( s.value().is_block() );
        const ck2::block* p = s.value().as_block();

        if (strcmp(t, top_title) == 0)
            return p; // base case, terminate

        /* recursive case... */

        if (ck2::title_tier(t) <= top_title_tier)
            continue; // skip recursion, because the title's tier is too low

        p = find_title(top_title, p); // recurse into title block

        if (p != nullptr)
            return p; // cool, found in subtree, pass it upstream and terminate
    }

    return nullptr;
}


void find_titles_under(const ck2::block* p_root, strvec_t& found_titles) {

    for (const auto& s : *p_root) {
        if ( !(s.key().is_string() && ck2::looks_like_title( s.key().as_string() )) )
            continue;

        const char* t = s.key().as_string();
        found_titles.push_back(t);
        assert( s.value().is_block() );
        const ck2::block* p_block = s.value().as_block();

        if (ck2::title_tier(t) > ck2::TIER_BARON)
            find_titles_under(p_block, found_titles);
    }
}


void fill_county_to_id_map(const ck2::VFS& vfs,
                           const ck2::DefaultMap& dm,
                           const ck2::DefinitionsTable& def_tbl,
                           str2id_map_t& county_to_id_map) {

    char filename[256];
    uint id = 0;

    for (auto&& r : def_tbl) {
        ++id;

        if (dm.is_water_province(id)) // sea | major river
            continue;

        if (r.name.empty()) // wasteland | external
            continue;

        sprintf(filename, "history/provinces/%u - %s.txt", id, r.name.c_str());
        fs::path real_path;

        if (!vfs.resolve_path(&real_path, filename))
            continue;

        const char* county = nullptr;

        ck2::parser parse(real_path);

        for (const auto& s : *parse.root_block()) {
            if (s.key() == "title") {
                assert( s.value().is_string() && ck2::looks_like_title( s.value().as_string() ) );
                county = s.value().as_string();
                assert( ck2::title_tier(county) == ck2::TIER_COUNT );
            }
        }

        if (county == nullptr) {
            /* history file contained no title assignment.  it was probably
               blank (for a wasteland or something). we may want to warn the
               user about this, although the behavior of CK2's error.log
               would suggest that empty history files ought be used for
               wasteland (which is incorrect, causing confusion). */
            continue;
        }

        if (!county_to_id_map.insert( {county, id} ).second) {
            throw ck2::Error("County '{}' maps to both province {} and {} (at the least)!",
                             county, county_to_id_map[county], id);
        }
    }
}


void blank_title_history(const ck2::VFS& vfs, const strvec_t& deleted_titles) {

    path title_hist_oroot = OUT_ROOT_DIR / "history/titles";

    if (! create_directories(title_hist_oroot)) {
        // output directory preexisted, so we need to ensure that it's clean first

        for (const auto& e : directory_iterator(title_hist_oroot))
            remove(e.path());
    }

    for (const auto& title : deleted_titles) {

        std::string filename = title + ".txt";
        fs::path virt_path = "history/titles" / filename;

        fs::path real_path;

        if ( vfs.resolve_path(&real_path, virt_path) ) {

            /* there is indeed reason to add a blank file override for
               this title, so let's get on with it... */

            path title_hist_opath = OUT_ROOT_DIR / virt_path;
            FILE* f;

            if ( (f = fopen(title_hist_opath.string().c_str(), "w")) == nullptr )
                throw ck2::Error("Failed to blank title history: {}: {}",
                                 strerror(errno), title_hist_opath.generic_string());

            fclose(f);
            ++g_stats.n_title_hist_blanked;
        }
    }
}



lt_printer::lt_printer(const fs::path& p, const strvec_t& _top_titles, const ck2::block* p_root_block)
    : os(p.string()), top_titles(_top_titles), indent(0), in_code_block(false) {

    if (!os) throw ck2::Error("Could not write to file: {}", p.generic_string());

    os << "# -*- ck2.landed_titles -*-\n\n";
    print(*p_root_block);
}


void lt_printer::print(const ck2::block& b) {
    for (const auto& stmt : b) print(stmt);
}


void lt_printer::print(const ck2::list& l) {
    for (const auto& obj : l) {
        print(obj);
        os << ' ';
    }
}


void lt_printer::print(const ck2::statement& s) {
    const ck2::object& k = s.key();
    const ck2::object& v = s.value();

    bool opened_code_block = false;

    if (k.is_string() && v.is_block()) {
        /* I'm unhinged... */
        const std::string t{ k.as_string() };

        if (std::find(top_titles.begin(), top_titles.end(), t) != top_titles.end())
            return; // oh, and this is an AST subtree filter.

        if (!in_code_block && (k == "allow" || k == "gain_effect")) {
            in_code_block = true;
            opened_code_block = true;
        }
    }

    os << std::setfill(' ') << std::setw(indent * TAB_WIDTH) << "";
    print(k);
    os << " = ";

    /* note that the more correct approach to match my intention with force-quoting would involve loading
     * valid culture tags and simply hashing k into a map of them */

    bool force_quote = (
        k.is_string() && v.is_string() && // both k,v are strings
        !in_code_block && // we're not somewhere in an arbitrary code block
        v != "yes" && v != "no" && // v isn't boolean
        /* special keywords whose values we should not force-quote */
        k != "culture" && k != "religion" && k != "controls_religion" && k != "mercenary_type" &&
        k != "title" && k != "title_female" && k != "title_prefix" && k != "foa" && k != "foa_female" &&
        k != "graphical_culture" && k != "name_tier" && k != "holy_site" && k != "pentarchy"
    );

    if (force_quote) os << '"' << v.as_string() << '"';
    else print(v);

    if (opened_code_block) in_code_block = false;

    os << '\n';
}


void lt_printer::print(const ck2::object& o) {
    if (o.is_string()) {
        if (strpbrk(o.as_string(), " \t\r\n'"))
            os << '"' << o.as_string() << '"';
        else
            os << o.as_string();
    }
    else if (o.is_integer())
        os << o.as_integer();
    else if (o.is_date())
        os << o.as_date();
    else if (o.is_decimal())
        os << o.as_decimal();
    else if (o.is_block()) {
        if (o.as_block()->empty())
            os << "{}";
        else {
            os << "{\n";
            ++indent;
            print(*o.as_block());
            --indent;
            os << std::setfill(' ') << std::setw(indent * TAB_WIDTH) << "" << '}';
        }
    }
    else if (o.is_list()) {
        os << "{ ";
        print(*o.as_list());
        os << '}';
    }
    else
        assert(false && "Unhandled object type");
}
