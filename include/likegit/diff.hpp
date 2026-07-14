#pragma once

#include <string>
#include <string_view>
#include <vector>

enum class EditType { EQUAL, INSERT, DELETE };

struct Edit {
    EditType         type;
    int              index_a; // index in 'a' (-1 for INSERT)
    int              index_b; // index in 'b' (-1 for DELETE)
    std::string_view text;
};

std::vector<Edit> compute_diff(const std::vector<std::string_view>& a,
                               const std::vector<std::string_view>& b);
