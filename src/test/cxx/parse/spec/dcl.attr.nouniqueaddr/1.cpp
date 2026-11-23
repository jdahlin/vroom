// [dcl.attr.nouniqueaddr] Example 1 (paragraph 2), docs/cxx-spec/dcl/dcl.attr.nouniqueaddr.md
// needs: P7
// status: annotated

template<typename Key, typename Value,
         typename Hash, typename Pred, typename Allocator>
class hash_map {
  [[no_unique_address]] Hash hasher;
  [[no_unique_address]] Pred pred;
  [[no_unique_address]] Allocator alloc;
  Bucket *buckets;
  // ...
public:
  // ...
};
