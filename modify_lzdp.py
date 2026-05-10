import re

def process_file(filepath):
    with open(filepath, 'r', encoding='utf-8') as f:
        content = f.read()

    # Replacement 1: adding head and prev initialization
    old_init = r"""    using Node = ROLListNode<Triple>;
    std::vector<Node\*> dp\(in_len \+ 1, nullptr\);
    dp\[0\] = new Node\(0, Triple\(0, 0, 0\), nullptr\);

    for \(size_t pos = 0; pos < in_len; pos\+\+\) \{"""
    
    new_init = """    using Node = ROLListNode<Triple>;
    std::vector<Node*> dp(in_len + 1, nullptr);
    dp[0] = new Node(0, Triple(0, 0, 0), nullptr);

    std::vector<size_t> head;
    std::vector<size_t> prev;
    if (match_engine_ == 1) {
        head.assign(max_search_size_ + 1, SIZE_MAX);
        prev.assign(in_len, SIZE_MAX);
    }

    for (size_t pos = 0; pos < in_len; pos++) {
        if (match_engine_ == 1 && pos + 2 < in_len) {
            uint16_t hash_val = ((input[pos] << 10) ^ (input[pos + 1] << 5) ^ input[pos + 2]) & (max_search_size_);
            prev[pos] = head[hash_val];
            head[hash_val] = pos;
        }"""
    
    content = re.sub(old_init, new_init, content)

    # Replacement 2: modifying the matching block
    old_match = r"""        auto kmp_results = kmpSearch\(
            input\.begin\(\) \+ search_start, search_len,
            input\.begin\(\) \+ pos, look_len, range, get_min_match\(\)\);

        for \(auto& kr : kmp_results\) \{
            if \(kr\.offset == 0 \|\| kr\.length < get_min_match\(\)\) \{ continue; \}
            size_t target = pos \+ kr\.length;"""
            
    new_match = """        struct MatchRes { size_t offset; size_t length; };
        std::vector<MatchRes> match_results;

        if (match_engine_ == 0) {
            auto kmp_results = kmpSearch(
                input.begin() + search_start, search_len,
                input.begin() + pos, look_len, range, get_min_match());
            for (auto& kr : kmp_results) {
                match_results.push_back({kr.offset, kr.length});
            }
        } else {
            size_t match_pos = prev[pos]; 
            size_t chain_length = range * 8; 
            while (match_pos != SIZE_MAX && chain_length-- > 0) {
                size_t dist = pos - match_pos;
                if (dist > search_size || dist == 0) break;

                size_t match_len = 0;
                while (match_len < look_len && input[pos + match_len] == input[match_pos + match_len]) {
                    match_len++;
                }

                if (match_len >= get_min_match()) {
                    match_results.push_back({dist, match_len});
                }
                match_pos = prev[match_pos];
            }
            if (match_results.size() > range) {
                std::sort(match_results.begin(), match_results.end(), [](const MatchRes& a, const MatchRes& b) {
                    return a.length > b.length;
                });
                match_results.resize(range);
            }
        }

        for (auto& kr : match_results) {
            if (kr.offset == 0 || kr.length < get_min_match()) { continue; }
            size_t target = pos + kr.length;"""
            
    content = re.sub(old_match, new_match, content)
    
    with open(filepath, 'w', encoding='utf-8') as f:
        f.write(content)

process_file('D:\\AAA_C\\compression-tool\\src\\algorithm\\LZDP.cpp')
