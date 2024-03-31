#include <iostream>
#include <memory>

#include <map>
#include <vector>

#include <setjmp.h>

template <typename... T> void print(const T &...t) {
    (void)std::initializer_list<int>{(std::cout << t << "", 0)...};
    std::cout << "\n";
}

struct Node;
struct Traceable;
struct ObjectHeader;


// Objects tracing information: allocation pointer to header.
static std::map<Traceable *, ObjectHeader> traceInfo;

struct ObjectHeader {
    bool marked;
    size_t size;
};

/**
 * The `Traceable` struct is used as a base class
 * for any object which should be managed by GC.
 */
struct Traceable {
    ObjectHeader &getHeader() { return traceInfo.at(this); }

    static void *operator new(size_t size) {
        // Allocate a block:
        void *object = ::operator new(size);

        // Create an object header for it:
        auto header = ObjectHeader{.marked = false, .size = size};
        traceInfo.insert(std::make_pair((Traceable *)object, header));

        return object;
    }

    virtual ~Traceable(){};
};

struct Node : public Traceable {
    char name;

    Node *left;
    Node *right;

    Node(char name, Node *left = nullptr, Node *right = nullptr)
            : name(name), left(left), right(right) {
        print("Constructing Node ", name);
    }

    virtual ~Node() { print("Destroying Node ", name); }
};

void dump(const char *label) {
    print("\n------------------------------------------------");
    print(label);

    print("\n{");

    for (const auto &it : traceInfo) {
        auto node = reinterpret_cast<Node *>(it.first);

        print("  [", node->name, "] ", it.first, ": {.marked = ", it.second.marked,
              ", .size = ", it.second.size, "}, ");
    }

    print("}\n");
}

/**
 * Go through object fields, and see if we have any
 * which are recorded in the `traceInfo`.
 */
std::vector<Traceable *> getPointers(Traceable *object) {
    auto p = (uint8_t *)object;
    auto end = (p + object->getHeader().size);
    std::vector<Traceable *> result;
    while (p < end) {
        auto address = (Traceable *)*(uintptr_t *)p;
        if (traceInfo.count(address) != 0) {
            result.emplace_back(address);
        }
        p++;
    }
    return result;
}

/**
 * Frame pointer.
 */
intptr_t *__rbp;

/**
 * Stack pointer.
 */
intptr_t *__rsp;

/**
 * Main frame stack begin.
 */
intptr_t *__stackBegin;

#define __READ_RBP() __asm__ volatile("mov %0, x29" : "=r"(__rbp))
#define __READ_RSP() __asm__ volatile("mov %0, sp" : "=r"(__rsp))

/**
 * Initializes address of the main frame.
 */
void gcInit() {
    // `main` frame pointer:
    __READ_RBP();
    __stackBegin = (intptr_t *)*__rbp;
}

/**
 * Traverses the stacks to obtain the roots.
 */
std::vector<Traceable *> getRoots() {
    std::vector<Traceable *> result;

    // Some local variables (roots) can be stored in registers.
    // Use `setjmp` to push them all onto the stack.
    jmp_buf jb;
    setjmp(jb);

    __READ_RSP();
    auto rsp = (uint8_t *)__rsp;
    auto top = (uint8_t *)__stackBegin;

    while (rsp < top) {
        auto address = (Traceable *)*(uintptr_t *)rsp;
        if (traceInfo.count(address) != 0) {
            result.emplace_back(address);
        }
        rsp++;
    }

    return result;
}

void mark() {
    auto worklist = getRoots();

    while (!worklist.empty()) {
        auto o = worklist.back();
        worklist.pop_back();
        auto &header = o->getHeader();

        if (!header.marked) {
            header.marked = true;
            for (const auto &p : getPointers(o)) {
                worklist.push_back(p);
            }
        }
    }
}

void sweep() {
    auto it = traceInfo.begin();
    while (it != traceInfo.end()) {
        if (it->second.marked) {
            it->second.marked = false;
            ++it;
        } else {
            delete it->first;
            it = traceInfo.erase(it);
        }
    }
}

void gc() {
    mark();
    dump("After mark:");
    sweep();
    dump("After sweep:");
}

/*

   Graph:

     A        -- Root
    / \
   B   C
      / \
     D   E
        / \
       F   G
            \
             H

*/

Node *createGraph() {
    auto H = new Node('H');

    auto G = new Node('G', nullptr, H);
    auto F = new Node('F');

    auto E = new Node('E', F, G);
    auto D = new Node('D');

    auto C = new Node('C', D, E);
    auto B = new Node('B');

    auto A = new Node('A', B, C);

    return A; // Root
}

int main(int argc, char const *argv[]) {
    gcInit();
    auto A = createGraph();
    dump("Allocated graph:");

    // Detach the whole right sub-tree:
    A->right = nullptr;

    // Run GC:
    gc();

    // Manually destroy remaining stuff
    delete A->left;
    delete A;
    return 0;
}
