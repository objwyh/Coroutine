//
//  main.cpp
//  Coroutine
//
//  Created by 王逸含 on 2021/1/27.
//

#include <cstddef>
#include <iostream>
#include <memory>
#include <system_error>
#include <type_traits>
#include <utility>
#include <vector>
#include <queue>
#include <tuple>
#include <functional>

// reg[0] -> %r8
// reg[1] -> %r9
// reg[2] -> %r12
// reg[3] -> %r13
// reg[4] -> %r14
// reg[5] -> %r15
// reg[6] -> %rdx
// reg[7] -> %rcx
// reg[8] -> %rbx
// reg[9] -> %rax
// reg[10] -> %rbp
// reg[11] -> %rdi
// reg[12] -> %rsi
// reg[13] -> %rsp
// reg[14] -> return address
struct Context {
    void *reg[15];
    std::vector<char> mem;
    Context(void *(*func)(void*) = nullptr, void *args = nullptr) : mem(4096) {
        reg[14] = (void *)func;
        reg[13] = (char *)((uintptr_t)(&mem.back()) & ~15ull) - sizeof(void *);
        reg[11] = args;
    }
} ma;

extern "C" void swap_context(void*, void*) asm("swap_context");
asm(R"(
    swap_context:
        lea 0x08(%rsp), %r10
        mov 0x00(%rsp), %r11

        mov %r8,  0x00(%rdi)
        mov %r9,  0x08(%rdi)
        mov %r12, 0x10(%rdi)
        mov %r13, 0x18(%rdi)
        mov %r14, 0x20(%rdi)
        mov %r15, 0x28(%rdi)
        mov %rdx, 0x30(%rdi)
        mov %rcx, 0x38(%rdi)
        mov %rbx, 0x40(%rdi)
        mov %rax, 0x48(%rdi)
        mov %rbp, 0x50(%rdi)
        mov %rdi, 0x58(%rdi)
        mov %rsi, 0x60(%rdi)
        mov %r10, 0x68(%rdi)
        mov %r11, 0x70(%rdi)

        mov 0x00(%rsi),  %r8
        mov 0x08(%rsi),  %r9
        mov 0x10(%rsi), %r12
        mov 0x18(%rsi), %r13
        mov 0x20(%rsi), %r14
        mov 0x28(%rsi), %r15
        mov 0x30(%rsi), %rdx
        mov 0x38(%rsi), %rcx
        mov 0x40(%rsi), %rbx
        mov 0x48(%rsi), %rax
        mov 0x50(%rsi), %rbp
        mov 0x58(%rsi), %rdi
        mov 0x60(%rsi), %r10
        mov 0x68(%rsi), %rsp
        mov 0x70(%rsi), %r11
        
        mov %r10,       %rsi
        sub $0x08,      %rsp
        mov %r11, 0x00(%rsp)
        ret
)");

int my_co_create(Context **ctx, void*(*co)(void*), void* args);


template<typename T, typename Seq, T Begin>
struct integer_range_impl {};

template<typename T, T... Integers, T Begin>
struct integer_range_impl<T, std::integer_sequence<T, Integers...>, Begin> {
    using type = std::integer_sequence<T, Begin + Integers...>;
};

template<typename T, T Begin, T End>
struct integer_range {
    using type = typename integer_range_impl<
            T, std::make_integer_sequence<T, End - Begin>, Begin>::type;
};

template<typename T, T Begin, T End>
using integer_range_t = typename integer_range<T, Begin, End>::type;

template<std::size_t Begin, std::size_t End>
struct index_range {
    using type = integer_range_t<std::size_t, Begin, End>;
};

template<std::size_t Begin, std::size_t End>
using index_range_t = typename index_range<Begin, End>::type;

template<typename Callable, typename... Args>
struct invoke {
    static auto call(Callable &&c, Args&&... args) {
        return c(std::forward<Args>(args)...);
    }
};

template<typename T, typename Enable = void>
struct decay_args_type {
    using type = T;
};

template<typename T, std::size_t N>
struct decay_args_type<T (&)[N]> {
    using type = T*;
};

template<typename T, std::size_t N>
struct decay_args_type<const T (&)[N]> {
    using type = const T*;
};

template<typename T, std::size_t N>
struct decay_args_type<T (&)[N], std::enable_if_t<std::is_same<T, char>::value>> {
    using type = std::string;
};

template<typename T>
decltype(auto) decay_copy(T&& t) {
    return std::decay_t<typename decay_args_type<T>::type>(std::forward<T>(t));
}

template <class Fp, class ...Args, size_t ...Indices>
inline void co_execute(std::tuple<Fp, Args...>& t, std::index_sequence<Indices...>) {
    invoke<Fp, Args...>::call(std::move(std::get<0>(t)), std::move(std::get<Indices>(t))...);
}

template <class Fp>
void* co_proxy(void* vp) {
    std::unique_ptr<Fp> p(static_cast<Fp*>(vp));
    using Index = index_range_t<1, std::tuple_size<Fp>::value>;
    co_execute(*p.get(), Index());
    return nullptr;
}

class Coroutine {
public:
    template<typename Fp, typename... Args>
    explicit Coroutine(Fp&& func, Args&&... args) {
        using Gp = std::tuple<typename std::decay<Fp>::type, typename std::decay<Args>::type...>;

        std::unique_ptr<Gp> p(new Gp(decay_copy(func), decay_copy(std::forward<Args>(args))...));

        int ec = my_co_create(&context, &co_proxy<Gp>, p.get());

        if (ec == 0) {
            (void)p.release();
        }
        else
            std::terminate();
    }

    Context *getContext() const {
        return context;
    }

private:
    Context *context;
};

struct Task {
    bool alive;
    Coroutine *coroutine;
};

std::queue<Task> taskManager;

Coroutine *current = nullptr;

template<typename Fp, typename... Args>
Coroutine my_co_await(Fp&& fp, Args&&... args) {
    return Coroutine(std::forward<Fp>(fp), std::forward<Args>(args)...);
}

int my_co_create(Context **ctx, void*(*co)(void*), void* args) {
    if (ctx == nullptr || co == nullptr) {
        return 1;
    }
    *ctx = new Context(co, args);
    return 0;
}

void co_resume(Coroutine &co) {
    current = &co;
    swap_context(&ma, current->getContext());
}

void co_yeild() {
    taskManager.push(Task{.alive = true, .coroutine = current});
    swap_context(current->getContext(), &ma);
}

void my_co_return() {
    swap_context(current->getContext(), &ma);
}

void event_loop() {

    while (true) {

        if (!taskManager.empty()) {

            Task task = taskManager.front();
            taskManager.pop();

             if (!task.alive)
                break;

            co_resume(*task.coroutine);

        } else {
            std::cout << "All coroutine exit" << std::endl;
            return;
        }

    }
}

void func1(int num) {
    int i = 0;
    while (i < num) {
        std::cout << "Coroutine func1 " << i << " times loop" << std::endl;
        co_yeild();
        i++;
    }
    std::cout << "Coroutine 1 exit" << std::endl;
    my_co_return();
}

void func2(double num, int step) {
    int i = 0;
    while (i < num) {
        std::cout << "Coroutine func2 " << i << " times loop" << std::endl;
        co_yeild();
        i += step;
    }
    std::cout << "Coroutine 2 exit" << std::endl;
    my_co_return();
}

void func3(int num, const std::string &msg) {
    int i = 0;
    while (i < 3) {
        std::cout << "Coroutine func3 " << i << " times loop msg = " << msg << std::endl;
        co_yeild();
        i++;
    }
    std::cout << "Coroutine 3 exit" << std::endl;
    my_co_return();
}

int main() {
    Coroutine co1 = my_co_await(func1, 2);
    std::cout << "Create co1\n";
    co_resume(co1);

    Coroutine co2 = my_co_await(func2, 10, 2);
    std::cout << "Create co2\n";
    co_resume(co2);

    Coroutine co3 = my_co_await(func3, 4, "Hello World");
    std::cout << "Create co3\n";
    co_resume(co3);

    event_loop();

    return 0;
}
