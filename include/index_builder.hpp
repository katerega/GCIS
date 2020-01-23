#include "divsufsort.h"
#include "gcis_eliasfano_index.hpp"
#include "sdsl/bit_vectors.hpp"
#include "sdsl/inv_perm_support.hpp"
#include "sdsl/lcp.hpp"
#include "sdsl/rmq_support.hpp"
#include "sdsl/wavelet_trees.hpp"

namespace gcis {

class elias_fano_grammar;

struct rule_info {
    // Original rule label
    uint_t id;
    // Starting position in the rev(text) of the rule's expansion
    uint_t pos;
    // The lenght of this rule
    uint_t len;
};

struct suffix_info {
    // Preorder id of the node
    uint_t id;
    // Previous sibling of the rule
    uint_t prev_rule;
    // Starting position in the text of the rule's expansion
    uint_t pos;
    // The lenght of the suffix expansion
    uint_t len;
};

/**
 * @brief This class is responsible for sorting the rules or the rules suffixes
 * expansion according
 * to the reverse lexicographical order (lexicographical order).
 */

template <class info_t = rule_info> class sorter {
  public:
    void sort(std::vector<info_t> &v, char *text) {
        pre_process(v);
        build_data_structures(text);
        std::sort(v.begin(), v.end(),
                  [this](const info_t &lhs, const info_t &rhs) {
                      auto rmq = m_rmq(m_ISA[lhs.pos], m_ISA[lhs.pos]);
                      if (lhs.len <= rmq) {
                          // lhs is a prefix of rhs
                          return true;
                      } else if (rhs.len <= rmq) {
                          // rhs is a prefix os lhs
                          return false;
                      } else {
                          /***
                           * Neither is a prefix of the other. Use ISA to find
                           *the order
                           ***/
                          return m_ISA[lhs.pos] < m_ISA[rhs.pos];
                      }
                  });
    }

  private:
    virtual void pre_process(std::vector<info_t> &v) {}

    /**
     * @brief Builds the suffix array, the inverse suffix array,
     * the LCP array and the RMQ support in order to be able to
     * sort the rules.
     *
     * @param text the text.
     */
    virtual void build_data_structures(char *text) {

        /***
         * Computes the text reverse
         */
        uint_t text_size = strlen(text);
        char *rev_text = new char[text_size + 1];
        for (uint_t i = 0; i < text_size; i++) {
            rev_text[i] = text[text_size - i - 1];
        }
        rev_text[text_size] = 0;
        // Builds suffix array
        sdsl::algorithm::calculate_sa((const unsigned char *)rev_text,
                                      text_size + 1, m_SA);
        // Builds the inverse suffix array
        m_ISA = sdsl::inv_perm_support<>(&m_SA);
        // Builds the LCP information
        sdsl::construct_im(m_lcp, text, sizeof(unsigned char));
        // Builds the RMQ Support.
        m_rmq = sdsl::rmq_succinct_sada<>(&m_lcp);
        // The SA  and the reverse text are not needed anymore
        sdsl::util::clear(m_SA);
        delete[] rev_text;
    }

    /**
     * @brief Compare two rules and returns true if rev(lhs)<rev(rhs)
     *
     * @param lhs first rule
     * @param rhs second rule
     * @return true if rev(lhs) < rev(rhs)
     * @return false otherwise
     */
    bool compare(const info_t &lhs, const info_t &rhs) const {
        auto rmq = m_rmq(m_ISA[lhs.pos], m_ISA[lhs.pos]);
        if (lhs.len <= rmq) {
            // lhs is a prefix of rhs
            return true;
        } else if (rhs.len <= rmq) {
            // rhs is a prefix os lhs
            return false;
        } else {
            /***
             * Neither is a prefix of the other. Use ISA to find the order
             ***/
            return m_ISA[lhs.pos] < m_ISA[rhs.pos];
        }
    }
    sdsl::int_vector<> m_SA;
    sdsl::inv_perm_support<> m_ISA;
    sdsl::lcp_bitcompressed<> m_lcp;
    sdsl::rmq_succinct_sada<> m_rmq;
};

/***
 * Specializes the methods for sorting the suffixes
 ***/
template <>
void sorter<suffix_info>::pre_process(std::vector<suffix_info> &v) {}
template <> void sorter<suffix_info>::build_data_structures(char *text) {

    uint_t text_size = strlen(text);
    // Builds suffix array
    sdsl::algorithm::calculate_sa((const unsigned char *)text, text_size + 1,
                                  m_SA);
    // Builds the inverse suffix array
    m_ISA = sdsl::inv_perm_support<>(&m_SA);
    // Builds the LCP information
    sdsl::construct_im(m_lcp, text, sizeof(unsigned char));
    // Builds the RMQ Support.
    m_rmq = sdsl::rmq_succinct_sada<>(&m_lcp);
    // The SA  and the reverse text are not needed anymore
    sdsl::util::clear(m_SA);
}

class elias_fano_dfs_helper {
  public:
    elias_fano_dfs_helper(sdsl::int_vector<> &rules_derivation,
                          sdsl::int_vector<> &rules_pos,
                          sdsl::int_vector<> &rules_expansion_pos,
                          sdsl::int_vector<> &suffixes_expansion_pos,
                          sdsl::bit_vector &focc, sdsl::bit_vector &l,
                          sdsl::bit_vector &bv_dfuds, sdsl::bit_vector &t,
                          sdsl::int_vector<> &pi, std::vector<int_t> &inv_pi,
                          sdsl::int_vector<> &wt, string &str,
                          std::vector<uint_t> &rules_expansion_len,
                          sdsl::int_vector<> &prev_rule, uint_t root,
                          uint_t &bv_idx)
        : m_rules_derivation(rules_derivation), m_rules_pos(rules_pos),
          m_rules_expansion_pos(rules_expansion_pos),
          m_suffixes_expansion_pos(suffixes_expansion_pos), m_focc(focc),
          m_l(l), m_bv_dfuds(bv_dfuds), m_t(t), m_pi(pi), m_inv_pi(inv_pi),
          m_wt(wt), m_str(str), m_rules_expansion_len(rules_expansion_len),
          m_prev_rule(prev_rule), m_root(root), m_bv_idx(bv_idx) {}

    /**
     * @brief Overloaded version of dfs method.
     * Starts the dfs by the root node.
     */
    void dfs() {
        /***
         * Make a dfs from every children of the root
         */
        uint_t pos = m_rules_pos[m_root];
        uint_t len = m_rules_pos[m_root + 1] - pos;
        uint_t offset = 0;
        for (uint_t i = pos; i < pos + len; i++) {
            m_suffixes_expansion_pos[global_dfs_idx] = offset;
            uint_t node = m_rules_derivation[i];
            if (i > pos) {
                m_prev_rule[global_dfs_idx] = m_rules_derivation[i - 1];
            }
            dfs(node, offset);
            offset += m_rules_expansion_len[node];
            global_dfs_idx++;
        }
    }

  private:
    uint_t dfs(uint_t rule_idx);
    sdsl::int_vector<> &m_rules_pos;
    sdsl::int_vector<> &m_rules_derivation;
    sdsl::bit_vector &m_focc;
    sdsl::bit_vector &m_l;
    sdsl::bit_vector &m_bv_dfuds;
    sdsl::bit_vector &m_t;
    sdsl::int_vector<> &m_pi;
    std::vector<int_t> &m_inv_pi;
    sdsl::int_vector<> &m_wt;
    std::vector<uint_t> &m_rules_expansion_len;
    sdsl::int_vector<> &m_rules_expansion_pos;
    sdsl::int_vector<> &m_suffixes_expansion_pos;
    sdsl::int_vector<> &m_prev_rule;
    string &m_str;

    uint_t m_root; // Root node

    uint_t &m_bv_idx; // dfuds index

    uint_t pi_idx = 257;         // permutation index
    uint_t focc_idx = 257;       // first occ bv index
    uint_t dfs_idx = 257;        // pre-order index
    uint_t leaf_idx = 0;         // leaf index
    uint_t wt_idx = 0;           // wavelet tree index
    uint_t l_idx = 0;            // L bitvector index
    uint_t global_dfs_idx = 257; // global dfs_index

    /**
     * @brief Do a dfs rooted in rule_idx and returns the length of the
     * leftmost subtree expansion. It computes the data structures from
     * index_basic.
     *
     * @param rule_idx the root of the dfs
     * @ param offset offset from the text beginning.
     */
    void dfs(uint_t rule_idx, uint_t offset) {
        if (rule_idx < 256) {
            /**
             * It is a terminal.
             * We mark m_t and add the symbol into m_str.
             */
            m_t[leaf_idx++] = 1;
            m_str.push_back(rule_idx);
            m_focc[focc_idx++] = 0;
            m_bv_dfuds[m_bv_idx++] = 0;
            m_l[l_idx++] = 1;
        } else {
            // It is not a terminal
            if (m_inv_pi[rule_idx] == -1) {
                /**
                 * It is the first time that this non-terminal appeared.
                 * The right-hand side it is extracted
                 */
                uint_t pos = m_rules_pos[rule_idx];
                uint_t len = m_rules_pos[rule_idx + 1] - pos;

                // Stores the non-terminal into the permutation and its
                // inverse.
                m_pi[pi_idx++] = rule_idx;
                m_inv_pi[rule_idx] = dfs_idx++;
                // Marks the node as the first occ
                m_focc[focc_idx++] = 1;
                // Will expand into non-terminal symbols
                uint_t local_offset = offset;

                // update the tree
                m_bv_dfuds[m_bv_idx + len] = 0;
                m_bv_idx += len + 1;

                for (int_t i = pos; i < pos + len; i++) {
                    global_dfs_idx++;
                    if (i > pos) {
                        m_prev_rule[global_dfs_idx] = m_rules_derivation[i - 1];
                        m_suffixes_expansion_pos[global_dfs_idx] = local_offset;
                    }
                    dfs(m_rules_derivation[i], local_offset);
                    local_offset +=
                        m_rules_expansion_len[m_rules_derivation[i]];
                }
                m_rules_expansion_pos[rule_idx] = offset;
            } else {
                /**
                 * It is a non-terminal and it has been already seem.
                 */
                leaf_idx++;
                // Put the rule_idx in the Wavelet Tree
                m_wt[wt_idx++] = rule_idx;
                m_bv_dfuds[m_bv_idx++] = 0;
                m_focc[focc_idx++] = 0;
                m_l[l_idx] = 1;
                l_idx += m_rules_expansion_len[rule_idx];
            }
        }
    }
};

template <class grammar_t, class sparse_bv_t = sdsl::sd_vector<>,
          class sparse_bv_t2 = sdsl::sd_vector<>>
class index_basics {
  public:
    index_basics(grammar_t &gref, char *text) : m_gref(gref), m_text(text) {
        dfs();
    }

    void dfs() {}

    grammar_t &m_gref;
    sdsl::int_vector<> m_X;
    sdsl::int_vector<> m_pi;
    sdsl::wt_gmr<> m_wt;
    sparse_bv_t m_focc;
    sparse_bv_t2 m_t;
    sparse_bv_t m_l;
    std::string m_str;
    sdsl::bit_vector m_bv_dfuds;

    char *m_text;
};

template <> void index_basics<elias_fano_grammar>::dfs() {
    // Decompress all the rules
    uint_t total_rules;
    total_rules = std::accumulate(m_gref.m_info.m_number_of_rules.begin(),
                                  m_gref.m_info.m_number_of_rules.end(), 0);
    // fix for first level, if we have rules Xc->c this is not necessary
    sdsl::int_vector<> rules_derivation(m_gref.m_info.m_grammar_size, 0,
                                        sdsl::bits::hi(total_rules) + 1);
    sdsl::int_vector<> rules_pos(
        total_rules + 1, 0, sdsl::bits::hi(m_gref.m_info.m_grammar_size) + 1);
    auto t =
        sdsl::bit_vector(m_gref.m_info.m_grammar_size - total_rules + 1, 0);
    // Decompress sequentially each rule

    // Special case to avoid ifs
    uint_t idx = 0;
    rules_derivation[idx] = 0;
    rules_pos[idx++] = 0;
    uint_t rule_concat_idx = 1;

    uint_t prev_lcp_pos = 0;
    uint_t prev_rule_pos = 1;
    uint_t prev_rule_len = 0;

    /**
     * Decompress the grammar and put everything into a single concatenated
     * array. We use rules_pos to identify where the right-hand side of each
     * rule begins.
     */
    for (uint_t i = 1; i < total_rules; i++) {
        rules_pos[i] = idx;
        auto cur_lcp_pos = m_gref.rules_lcp_select(i + 1);
        uint_t lcp_len = cur_lcp_pos - prev_lcp_pos - 1;
        prev_lcp_pos = cur_lcp_pos;

        auto cur_rule_pos = m_gref.rules_delim_select(i + 1);
        uint_t suffix_len = cur_rule_pos - prev_rule_pos - 1;
        prev_rule_pos = cur_rule_pos;

        auto cur_rule_len = lcp_len + suffix_len;
        m_gref.copy_lcp(rules_derivation, lcp_len, prev_rule_len, idx);
        m_gref.copy_suffix(rules_derivation, suffix_len, rule_concat_idx, idx);
        prev_rule_len = cur_rule_len;
    }
    rules_pos[total_rules] = m_gref.m_info.m_grammar_size;

    /**
     * Computes the expansion for every rule in bfs order
     */
    std::vector<uint_t> rules_expansion_len(total_rules, 0);
    // Special case: terminal rules have length 1
    std::fill_n(rules_expansion_len.begin(), 256, 1);
    for (uint_t i = 256; i < total_rules; i++) {
        uint_t pos = rules_pos[i];
        uint_t len = rules_pos[i + 1] - pos;
        /***
         * The expansion for a nonterminal is the sum of the expansions
         * of its right-hand side
         ***/
        for (uint_t j = pos; j < pos + len; j++) {
            rules_expansion_len[i] += rules_expansion_len[rules_derivation[j]];
        }
    }

    auto l = sdsl::bit_vector(m_gref.m_info.m_text_size[1], 0);
    idx = 0;
    uint_t root_arity = rules_pos[m_gref.m_xs + 1] - rules_pos[m_gref.m_xs];
    uint_t bv_idx = 512 + 3 + root_arity + 1;
    int_t stack_idx = 0;
    m_pi.resize(total_rules);
    /***
     * Inverse permutation: indicates whether the rules already appeared or
     * not. In affirmative case, points to an index in the original
     * permutation
     */
    std::vector<int_t> inv_pi(total_rules, -1);
    auto focc = sdsl::bit_vector(m_gref.m_info.m_grammar_size + 1, 0);

    /***
     * Int vector which will store the wavelet tree before it is constructed
     */
    sdsl::int_vector<> wt(m_gref.m_info.m_grammar_size - total_rules + 1 -
                          (m_gref.m_info.m_first_level_expansion_len));

    // Account for the root and the terminal leaves
    m_bv_dfuds =
        sdsl::bit_vector(3 + 2 * (m_gref.m_info.m_grammar_size + 1) - 1, 1);
    m_bv_dfuds[2] = 0;
    /**
     * Initialize the dfuds tree with the information from the root and the
     * terminal leaves.
     */
    for (uint_t i = 3 + root_arity + 256; i <= 512 + 3 + root_arity; i++) {
        m_bv_dfuds[i] = 0;
    }

    /**
     * Initializes the permutation and its inverse. The first occurrence
     * bitvector is also initialized.
     */
    inv_pi[m_gref.m_xs] = 0;
    m_pi[0] = m_gref.m_xs;
    focc[0] = 1;
    for (int i = 0; i < 256; i++) {
        inv_pi[i] = i + 1;
        m_pi[i + 1] = i;
        focc[i + 1] = 1;
        rules_expansion_len[i] = 1;
    }

    /***
     * This will store the starting position in the texto of each
     * rule expansion
     */
    sdsl::int_vector<> rules_expansion_pos(
        total_rules, 0, sdsl::bits::hi(m_gref.m_info.m_text_size[1]) + 1);

    /***
     * This will store the starting position in the text of each rule suffix
     * expansion
     */
    sdsl::int_vector<> suffixes_expansion_pos(
        m_gref.m_info.m_grammar_size, 0,
        sdsl::bits::hi(m_gref.m_info.m_text_size[1]) + 1);
    /**
     * This will store the previous sibling of each suffix of a right-hand
     */
    sdsl::int_vector<> prev_rule(m_gref.m_info.m_grammar_size, 0,
                                 sdsl::bits::hi(total_rules) + 1);

    elias_fano_dfs_helper dfs_h(
        rules_derivation, rules_pos, rules_expansion_pos,
        suffixes_expansion_pos, focc, l, m_bv_dfuds, t, m_pi, inv_pi, wt, m_str,
        rules_expansion_len, prev_rule, m_gref.m_xs, bv_idx);
    dfs_h.dfs();
    sdsl::construct_im(m_wt, wt);
    m_l = l;
    m_focc = focc;
    m_t = t;
    /***
     * Construct rules info array
     */
    std::vector<rule_info> rules(m_pi.size() - 256);
    rules[0].id = m_gref.m_xs;
    rules[0].len = m_gref.m_info.m_text_size[1];
    rules[0].pos = 0;
    cout << rules[0].id << " " << rules[0].len << " " << rules[0].pos << endl;

    for (uint_t i = 257; i < m_pi.size(); i++) {
        rules[i - 256].id = m_pi[i];
        rules[i - 256].len = rules_expansion_len[m_pi[i]];
        rules[i - 256].pos = rules_expansion_pos[m_pi[i]];
        cout << i - 256 << " " << rules[i - 256].id << " " << rules[i - 256].len
             << " " << rules[i - 256].pos << endl;
    }

    sorter<rule_info> s;
    s.sort(rules, m_text);
}

} // namespace gcis