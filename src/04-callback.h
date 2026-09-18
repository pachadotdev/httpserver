#ifndef HTTPSERVER_04_CALLBACK_H
#define HTTPSERVER_04_CALLBACK_H

#include <functional>
#include <later2_api.h>

class Callback {
public:
  virtual ~Callback() {};
  virtual void operator()() = 0;
};

// If the Callback class were integrated into later, this wouldn't be
// necessary -- later could accept a void(Callback*) function.
void invoke_callback(void *data);

// Wrapper class for std functions
class StdFunctionCallback : public Callback {
private:
  std::function<void(void)> fun;

public:
  StdFunctionCallback(std::function<void(void)> fun) : fun(fun) {}

  void operator()() { fun(); }
};

void invoke_later(std::function<void(void)> f, double secs = 0);

// Invoke a callback and delete the object. The Callback object must have been
// heap-allocated.
void invoke_callback(void *data) {
  Callback *cb = reinterpret_cast<Callback *>(data);
  (*cb)();
  delete cb;
}

// Schedule a std::function<void(void)> to be invoked with later().
void invoke_later(std::function<void(void)> f, double secs) {
  StdFunctionCallback *b_fun = new StdFunctionCallback(f);
  later2::later(invoke_callback, (void *)b_fun, secs);
}

#endif
