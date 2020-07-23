#include <iostream>
#include <array>
#include <algorithm>
#include <cstring>

#define FOUND (1u<<16u)
#define FOUND_MASK (FOUND - 1u)
#define GO_DOWN (1u<<24u)
#define GO_DOWN_MASK (GO_DOWN - 1u)
#define DEBUG_MODE
#ifdef DEBUG_MODE

#include <vector>
#include <algorithm>
#include <iomanip>
#include <cassert>

#define ASSERT(x) assert(x)
#else
#define ASSERT(x)
#endif

template<typename K>
struct default_compare {
    inline int operator()(const K &a, const K &b) {
        if constexpr(std::is_floating_point_v<K> || std::is_signed_v<K>) {
            return a - b;
        } else return (a < b) ? -1 : ((a > b) ? 1 : 0);
    }
};

template<typename K, typename V, size_t B = 6, typename Compare = default_compare<K>>
class BTree;

template<typename K, typename V, size_t B = 6, typename Compare = default_compare<K>>
class AbstractBTNode;

template<typename K, typename V, bool IsInternal, typename Compare = default_compare<K>, size_t B = 6>
struct alignas(64) BTreeNode;

template<typename K, typename V, size_t B, typename Compare>
struct AbstractBTNode {

    static Compare comp;

    struct SplitResult {
        AbstractBTNode *l, *r;
        K key;
        V value;
    };

    struct iterator {
        uint16_t idx;
        AbstractBTNode *node;

        bool operator==(const iterator &that) noexcept {
            return idx == that.idx && node == that.node;
        }

        bool operator!=(const iterator &that) noexcept {
            return idx != that.idx || node != that.node;
        }

        iterator operator++(int) {
            return node->successor(idx);
        }

        iterator &operator++() {
            auto res = node->successor(idx);
            this->node = res.node;
            this->idx = res.idx;
            return *this;
        }

        std::pair<const K &, V &> operator*() {
            return {node->key_at(idx), node->value_at(idx)};
        }
    };

    virtual bool member(const K &key) = 0;

    virtual std::optional<V> insert(const K &key, const V &value, AbstractBTNode **root) = 0;

    virtual void
    adopt(AbstractBTNode *l, AbstractBTNode *r, K key, V value, size_t position, AbstractBTNode **root) = 0;

    virtual AbstractBTNode *&node_parent() = 0;

    virtual uint16_t &node_idx() = 0;

    virtual uint16_t &node_usage() = 0;

    virtual iterator successor(uint16_t idx) = 0;

    virtual iterator predecessor(uint16_t idx) = 0;

    virtual iterator min() = 0;

    virtual iterator max() = 0;

    virtual const K &key_at(size_t) = 0;

    virtual V &value_at(size_t) = 0;

    virtual ~AbstractBTNode() = default;

#ifdef DEBUG_MODE

    virtual void display(size_t indent) = 0;

#endif

    friend BTree<K, V, B, Compare>;
};

template<typename K, typename V, size_t B, typename Compare>
class BTree {
    AbstractBTNode<K, V, B, Compare> *root = nullptr;
public:
    using iterator = typename AbstractBTNode<K, V, B, Compare>::iterator;
#ifdef DEBUG_MODE

    void display() {
        if (root) root->display(0);
    };
#endif

    std::optional<V> insert(const K &key, const V &value) {
        if (root == nullptr) {
            auto node = new BTreeNode<K, V, false, Compare, B>;
            node->usage = 1;
            node->keys[0] = key;
            node->values[0] = value;
            root = node;
            return std::nullopt;
        }
        return root->insert(key, value, &root);
    }

    const K &min_key() {
        auto iter = root->min();
        return iter.node->key_at(iter.idx);
    }

    const K &max_key() {
        auto iter = root->max();
        return iter.node->key_at(iter.idx);
    }

    iterator begin() {
        if (root)
            return root->min();
        return end();
    }

    iterator end() {
        return iterator{
                .idx = 0,
                .node = nullptr
        };
    }

    ~BTree() {
        delete root;
    }

};

template<typename K, typename V, bool IsInternal, typename Compare, size_t B>
struct alignas(64) BTreeNode : AbstractBTNode<K, V, B, Compare> {
    static_assert(2 * B < FOUND, "B is too large");
    static_assert(B > 0, "B is too small");
    using Node = AbstractBTNode<K, V, B, Compare>;
    using NodePtr = Node *;
    using SplitResult = typename AbstractBTNode<K, V, B, Compare>::SplitResult;

    K keys[2 * B - 1];
    V values[2 * B - 1];
    NodePtr children[IsInternal ? (2 * B) : 0];
    NodePtr parent = nullptr;
    uint16_t usage = 0;
    uint16_t parent_idx = 0;

    using LocFlag = uint;


    inline NodePtr &node_parent() override {
        return parent;
    }

    inline uint16_t &node_usage() override {
        return usage;
    }

    inline uint16_t &node_idx() override {
        return parent_idx;
    }

    inline LocFlag linear_search(const K &key) {
        ASSERT(usage < 2 * B);
        for (uint i = 0; i < usage; ++i) {
            auto res = Node::comp(key, keys[i]);
            if (res < 0) {
                return GO_DOWN | i;
            }
            if (res == 0) {
                return FOUND | i;
            }
        }
        return GO_DOWN | usage;
    }

    bool member(const K &key) override {
        ASSERT(usage < 2 * B);
        auto flag = linear_search(key);
        if (flag & FOUND) {
            return true;
        }
        if constexpr (IsInternal) {
            return children[flag & GO_DOWN_MASK]->member(key);
        } else return false;
    }

    typename Node::SplitResult split() {
        ASSERT(usage == 2 * B - 1);
        auto l = new BTreeNode;
        auto r = new BTreeNode;
        l->usage = r->usage = B - 1;
        l->parent = r->parent = this->parent;
        std::uninitialized_move(keys, keys + B - 1, l->keys);
        std::uninitialized_move(keys + B, keys + usage, r->keys);
        std::uninitialized_move(values, values + B - 1, l->values);
        std::uninitialized_move(values + B, values + usage, r->values);
        this->usage = 0;
        if constexpr (IsInternal) {
            std::memcpy(l->children, children, B * sizeof(NodePtr));
            std::memcpy(r->children, children + B, B * sizeof(NodePtr));
            for (size_t i = 0; i < B; ++i) {
                l->children[i]->node_parent() = l;
                l->children[i]->node_idx() = i;
                r->children[i]->node_parent() = r;
                r->children[i]->node_idx() = i;
            }
        }
        return SplitResult{
                .l = l,
                .r = r,
                .key = std::move(keys[B - 1]),
                .value = std::move(values[B - 1]),
        };
    }

    NodePtr singleton(NodePtr l, NodePtr r, K key, V value) {
        auto node = new BTreeNode<K, V, true, Compare, B>;
        node->usage = 1;
        node->values[0] = std::move(value);
        node->keys[0] = std::move(key);
        node->children[0] = l;
        l->node_idx() = 0;
        l->node_parent() = node;
        node->children[1] = r;
        r->node_idx() = 1;
        r->node_parent() = node;
        return node;
    }

    std::optional<V> insert(const K &key, const V &value, NodePtr *root) override {
        auto res = linear_search(key);
        if (res & FOUND) {
            V original = std::move(values[res & FOUND_MASK]);
            values[res & FOUND_MASK] = value;
            return {original};
        }
        auto position = res & GO_DOWN_MASK;
        if constexpr (IsInternal) {
            return children[position]->insert(key, value, root);
        } else {
            std::move_backward(values + position, values + usage, values + usage + 1);
            std::move_backward(keys + position, keys + usage, keys + usage + 1);
            values[position] = value;
            keys[position] = key;
            usage++;
            if (usage == 2 * B - 1) /* leaf if full */ {
                auto result = split();
                if (parent)
                    parent->adopt(result.l, result.r, std::move(result.key), std::move(result.value), parent_idx, root);
                else {
                    delete *root;
                    *root = singleton(result.l, result.r, std::move(result.key), std::move(result.value));
                }
            }
            return std::nullopt;
        }
    }

    void adopt(NodePtr l, NodePtr r, K key, V value, size_t position, NodePtr *root) override {
        std::move_backward(values + position, values + usage, values + usage + 1);
        std::move_backward(keys + position, keys + usage, keys + usage + 1);
        std::memmove(children + position + 1, children + position,
                     (usage + 1 - position) * sizeof(NodePtr));

        delete (children[position]);
        children[position] = l;
        l->node_parent() = this;
        children[position + 1] = r;
        r->node_parent() = this;
        for (int i = position; i < usage + 2; ++i) {
            children[i]->node_idx() = i;
        }
        values[position] = std::move(value);
        keys[position] = std::move(key);
        usage++;
        if (usage == 2 * B - 1) {
            auto result = split();
            if (parent) {
                parent->adopt(result.l, result.r, std::move(result.key), std::move(result.value), parent_idx, root);
            } else {
                delete *root;
                *root = singleton(result.l, result.r, std::move(result.key), std::move(result.value));
            }
        }
    }

#ifdef DEBUG_MODE

    void display(size_t ident) override {
        std::string idents(ident ? ident - 1 : 0, '-');
        if (ident) idents.push_back('>');
        if (ident) idents.push_back(' ');
        std::cout << idents << "node at " << this << ", parent: " << parent << ", index: " << parent_idx
                  << ", fields: ";
        {
            auto i = 0;
            for (; i < usage; ++i) {
                std::cout << " " << std::setw(4) << keys[i];
            }
            for (; i < 2 * B - 2; ++i) {
                std::cout << " " << std::setw(4) << "_";
            }
        }
        std::cout << std::endl;
        if constexpr (IsInternal) {
            for (auto i = 0; i <= usage; ++i) {
                children[i]->display(ident + 4);;
            }
        }
    }

#endif

    typename Node::iterator min() {
        if constexpr(IsInternal) {
            return children[0]->min();
        } else {
            return typename Node::iterator{
                    .idx = 0,
                    .node = this
            };
        }
    }

    typename Node::iterator max() {
        if constexpr(IsInternal) {
            return children[usage]->max();
        } else {
            return typename Node::iterator{
                    .idx = uint16_t(usage - 1u),
                    .node = this
            };
        }
    }

    const K &key_at(size_t i) override {
        return keys[i];
    };

    V &value_at(size_t i) override {
        return values[i];
    };

    typename Node::iterator predecessor(uint16_t idx) override {
        if constexpr (IsInternal) {
            return children[idx]->max();
        } else {
            if (idx)
                return typename Node::iterator{
                        .idx = uint16_t(idx - 1u),
                        .node = this
                };
            else {
                NodePtr node = this;
                while (node->node_parent() && node->node_idx() == 0) {
                    node = node->node_parent();
                }
                if (node->node_parent()) {
                    return typename Node::iterator{
                            .idx = uint16_t(node->node_idx() - 1u),
                            .node = node->node_parent()
                    };
                } else {
                    return typename Node::iterator{
                            .idx = 0,
                            .node = nullptr
                    };
                }
            }
        }
    }

    typename Node::iterator successor(uint16_t idx) override {
        if constexpr (IsInternal) {
            return children[idx + 1]->min();
        } else {
            if (idx < usage - 1)
                return typename Node::iterator{
                        .idx = uint16_t(idx + 1u),
                        .node = this
                };
            else {
                NodePtr node = this;
                while (node->node_parent() && node->node_idx() == node->node_parent()->node_usage()) {
                    node = node->node_parent();
                }
                if (node->node_parent()) {
                    return typename Node::iterator{
                            .idx = node->node_idx(),
                            .node = node->node_parent()
                    };
                } else {
                    return typename Node::iterator{
                            .idx = 0,
                            .node = nullptr
                    };
                }
            }
        }
    }

    ~BTreeNode() override {
        if constexpr(IsInternal) {
            if (usage)
                for (auto i = 0; i <= usage; ++i) {
                    delete children[i];
                }
        }
    }
};

template<class K, class V, size_t H>
using DefaultBTNode = BTreeNode<K, V, H>;

template<typename K, typename V, size_t B, typename Compare>
Compare AbstractBTNode<K, V, B, Compare>::comp{};

int main() {
    std::vector<int> a, b;
    BTree<int, int> test;
    for (int i = 0; i < 100; ++i) {
        auto k = rand();
        a.push_back(k);
        test.insert(k, k);
    }
    std::sort(a.begin(), a.end());
    a.erase(unique(a.begin(), a.end()), a.end());
    for (auto i : test) {
        b.push_back(i.first);
    }
    assert(a == b);
    test.display();
    return 0;
}
