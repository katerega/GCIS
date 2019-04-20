#include "../external/malloc_count/malloc_count.h"
#include "gcis.hpp"
#include "gcis_eliasfano.hpp"
#include "gcis_eliasfano_no_lcp.hpp"
#include "gcis_gap.hpp"
#include "gcis_s8b.hpp"
#include "gcis_unary.hpp"
#include "sais.h"
#include <cassert>
#include <cstring>
#include <fstream>
#include <iostream>

using namespace std::chrono;
using timer = std::chrono::high_resolution_clock;

void load_string_from_file(char *&str, char *filename) {
    std::ifstream f(filename, std::ios::binary);
    f.seekg(0, std::ios::end);
    uint64_t size = f.tellg();
    f.seekg(0, std::ios::beg);
    str = new char[size + 1];
    f.read(str, size);
    str[size] = 0;
    f.close();
};

int main(int argc, char *argv[]) {

#ifdef MEM_MONITOR
    mm.event("GC-IS Init");
#endif

    if (argc != 4) {
        std::cerr << "Usage: \n"
                  << "./gc-is-codec -c <file_to_be_encoded> <output>\n"
                  << "./gc-is-codec -d <file_to_be_decoded> <output>\n"
                  << "./gc-is-codec -s <file_to_be_decoded> <output>\n"
                  << "./gc-is-codec -l <file_to_be_decoded> <output>\n"
                  << "./gc-is-codec -A <input_file> <output>\n"
                  << "./gc-is-codec -e <encoded_file> <query file>\n";

        exit(EXIT_FAILURE);
    }

    // Dictionary type

    gcis_dictionary<gcis_eliasfano_codec> d;
    char *mode = argv[1];
    if (strcmp(mode, "-c") == 0) {
        char *str;
        load_string_from_file(str, argv[2]);
        std::ofstream output(argv[3], std::ios::binary);

#ifdef MEM_MONITOR
        mm.event("GC-IS Compress");
#endif

        auto start = timer::now();
        d.encode(str);
        auto stop = timer::now();

#ifdef MEM_MONITOR
        mm.event("GC-IS Save");
#endif

        cout << "input:\t" << strlen(str) << " bytes" << endl;
        cout << "output:\t" << d.size_in_bytes() << " bytes" << endl;

        d.serialize(output);
        delete[] str;
        output.close();
        cout << "time: " << (double)duration_cast<seconds>(stop - start).count()
             << " seconds" << endl;
    } 
    else if (strcmp(mode, "-d") == 0) {
        std::ifstream input(argv[2]);
        std::ofstream output(argv[3], std::ios::binary);

#ifdef MEM_MONITOR
        mm.event("GC-IS Load");
#endif

        d.load(input);

#ifdef MEM_MONITOR
        mm.event("GC-IS Decompress");
#endif

        auto start = timer::now();
        char *str = d.decode();
        auto stop = timer::now();

        output.write(str, strlen(str));
        input.close();
        output.close();

        cout << "input:\t" << d.size_in_bytes() << " bytes" << endl;
        cout << "output:\t" << strlen(str) << " bytes" << endl;
        cout << "time: " << (double)duration_cast<milliseconds>(stop - start).count()/1000.0
             << setprecision(2) << fixed << " seconds" << endl;
    } 
    else if (strcmp(mode, "-s") == 0) {

        std::ifstream input(argv[2]);
        std::ofstream output(argv[3], std::ios::binary);

#ifdef MEM_MONITOR
        mm.event("GC-IS/SACA Load");
#endif

        d.load(input);

#ifdef MEM_MONITOR
        mm.event("GC-IS/SACA Decompress");
#endif

        uint_t *SA;
        std::cout << "Building SA under decoding." << std::endl;
        auto start = timer::now();
        unsigned char *str = d.decode_saca(&SA);
        auto stop = timer::now();
 
        size_t n = strlen((char*)str)+1;

#if CHECK
        if (!d.suffix_array_check(SA, str, (uint_t)n, sizeof(char), 0))
            std::cout << "isNotSorted!!\n";
        else
            std::cout << "isSorted!!\n";
#endif

#if CHECK

        cout << "input:\t" << d.size_in_bytes() << " bytes" << endl;
        cout << "output:\t" << n - 1 << " bytes" << endl;
        cout << "SA:\t" << n * sizeof(uint_t) << " bytes" << endl;
        output.write((const char*) &n,sizeof(n));
        output.write((const char*)SA,sizeof(sa_int32_t)*n);

#endif


        output.close();
        input.close();
        std::cout << "time: "
                  << (double)duration_cast<seconds>(stop - start).count()
                  << " seconds" << endl;
        delete[] SA;
    }
    else if (strcmp(mode, "-l") == 0) {

        std::ifstream input(argv[2]);
        std::ofstream output(argv[3], std::ios::binary);

#ifdef MEM_MONITOR
        mm.event("GC-IS/SACA+LCP Load");
#endif

        d.load(input);

#ifdef MEM_MONITOR
        mm.event("GC-IS/SACA_LCP Decompress");
#endif

        uint_t *SA;
        int_t *LCP;
        std::cout << "Building SA+LCP under decoding." << std::endl;
        auto start = timer::now();
        unsigned char *str = d.decode_saca_lcp(&SA, &LCP);
        auto stop = timer::now();
 
        size_t n = strlen((char*)str)+1;

#if CHECK
        if (!d.suffix_array_check(SA, str, (uint_t)n, sizeof(char), 0))
            std::cout << "isNotSorted!!\n";
        else
            std::cout << "isSorted!!\n";

        if (!d.lcp_array_check(SA, LCP, str, (uint_t)n, sizeof(char), 0))
            std::cout << "isNotLCP!!\n";
        else
            std::cout << "isLCP!!\n";
#endif

#if CHECK

        cout << "input:\t" << d.size_in_bytes() << " bytes" << endl;
        cout << "output:\t" << n - 1 << " bytes" << endl;
        cout << "SA:\t" << n * sizeof(uint_t) << " bytes" << endl;
        cout << "LCP:\t" << n * sizeof(uint_t) << " bytes" << endl;
        output.write((const char*) &n,sizeof(n));
        output.write((const char*)SA,sizeof(sa_int32_t)*n);
        output.write((const char*)LCP,sizeof(sa_int32_t)*n);

#endif

        output.close();
        input.close();
        std::cout << "time: "
                  << (double)duration_cast<seconds>(stop - start).count()
                  << " seconds" << endl;
        delete[] SA;
        delete[] LCP;

    } 
    else if (strcmp(mode, "-A") == 0) {

        char *str;
        load_string_from_file(str, argv[2]);

        size_t n = strlen(str) + 1;
        uint_t *SA = new uint_t[n];

        std::cout << "Building SA with SAIS ." << std::endl;
        auto start = timer::now();
        // sais_u8((sa_uint8_t*) str,SA,n,k);
        d.saca(str, SA, n);
        auto stop = timer::now();

#if CHECK
        if (!d.suffix_array_check(SA, (unsigned char *)str, (uint_t)n,
                                  sizeof(char), 0))
            std::cout << "isNotSorted!!\n";
        else
            std::cout << "isSorted!!\n";
        std::ofstream output(argv[3], std::ios::binary);
        output.write((const char*) &n,sizeof(n));
        output.write((const char*)SA,sizeof(sa_int32_t)*n);
        output.close();
#endif
 

        // d.suffix_array_write((uint_t *)SA, n, argv[2], "SA");

        cout << "input:\t" << n << " bytes" << endl;
        cout << "SA:\t" << n * sizeof(uint_t) << " bytes" << endl;

        cout << "time: " << (double)duration_cast<seconds>(stop - start).count()
             << " seconds" << endl;
    } else if (strcmp(mode, "-e") == 0) {
        std::ifstream input(argv[2], std::ios::binary);
        std::ifstream query(argv[3]);

#ifdef MEM_MONITOR
        mm.event("GC-IS Load");
#endif

        d.load(input);

#ifdef MEM_MONITOR
        mm.event("GC-IS Extract");
#endif
        vector<pair<int,int>> v_query;
        uint64_t l, r;
        while (query >> l >> r) {
                v_query.push_back(make_pair(l,r));
        }
        d.extract_batch(v_query);
    } 
    else {  
        std::cerr << "Invalid mode, use: " << endl <<
                  "-c for compression;" << endl <<
                  "-d for decompression;" << endl <<
                  "-e for extraction;" << endl <<
                  "-s for building SA under decompression" << endl <<
                  "-A for building SA with SAIS" << endl;
        exit(EXIT_FAILURE);
    }

#ifdef MEM_MONITOR
    mm.event("GC-IS Finish");
#endif

    return 0;
}
