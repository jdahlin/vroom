/* A declaration the parser keeps as tokens (an attribute-specifier-seq
   after a using-declarator, clang's extension): the note says where the
   parse stopped and what it expected. */
int x;
namespace n {
using ::x [[deprecated]];
}
int y = n::x;
