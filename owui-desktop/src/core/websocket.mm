#include "core/websocket.h"

#import <Foundation/Foundation.h>

#include <atomic>
#include <mutex>

#include "core/common.h"

namespace ws {

struct Socket::Impl : std::enable_shared_from_this<Socket::Impl> {
  NSURLSession* session = nil;
  NSURLSessionWebSocketTask* task = nil;
  MessageHandler onMessage;
  StateHandler onState;
  std::mutex mutex;
  std::atomic<bool> open{false};
  std::atomic<bool> closed{false};

  void receiveNext() {
    if (!task || closed.load()) return;
    NSURLSessionWebSocketTask* current = task;
    std::weak_ptr<Impl> weak = shared_from_this();
    [current receiveMessageWithCompletionHandler:^(NSURLSessionWebSocketMessage* message,
                                                   NSError* error) {
      std::shared_ptr<Impl> self = weak.lock();
      if (!self || self->closed.load()) return;
      if (error) {
        self->open.store(false);
        StateHandler handler;
        {
          std::lock_guard<std::mutex> lock(self->mutex);
          handler = self->onState;
        }
        if (handler) handler(false, error.localizedDescription.UTF8String);
        return;
      }
      if (message && message.type == NSURLSessionWebSocketMessageTypeString && message.string) {
        MessageHandler handler;
        {
          std::lock_guard<std::mutex> lock(self->mutex);
          handler = self->onMessage;
        }
        if (handler) handler(message.string.UTF8String);
      }
      self->receiveNext();
    }];
  }
};

Socket::Socket() : impl_(std::make_shared<Impl>()) {}

Socket::~Socket() { close(); }

void Socket::setHandlers(MessageHandler onMessage, StateHandler onState) {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  impl_->onMessage = std::move(onMessage);
  impl_->onState = std::move(onState);
}

void Socket::connect(const std::string& url,
                     const std::vector<std::pair<std::string, std::string>>& headers) {
  MessageHandler savedMessage;
  StateHandler savedState;
  {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    savedMessage = impl_->onMessage;
    savedState = impl_->onState;
  }
  close();
  impl_ = std::make_shared<Impl>();
  setHandlers(std::move(savedMessage), std::move(savedState));
  NSURLSessionConfiguration* config = [NSURLSessionConfiguration ephemeralSessionConfiguration];
  config.timeoutIntervalForRequest = 60;
  impl_->session = [NSURLSession sessionWithConfiguration:config];

  NSMutableURLRequest* request = [NSMutableURLRequest
      requestWithURL:[NSURL URLWithString:[NSString stringWithUTF8String:url.c_str()]]];
  for (const auto& [key, value] : headers) {
    [request setValue:[NSString stringWithUTF8String:value.c_str()]
        forHTTPHeaderField:[NSString stringWithUTF8String:key.c_str()]];
  }

  impl_->task = [impl_->session webSocketTaskWithRequest:request];
  impl_->open.store(true);
  [impl_->task resume];
  impl_->receiveNext();

  StateHandler handler;
  {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    handler = impl_->onState;
  }
  if (handler) handler(true, "");
}

void Socket::send(const std::string& text) {
  if (!impl_->task) return;
  NSURLSessionWebSocketMessage* message = [[NSURLSessionWebSocketMessage alloc]
      initWithString:[NSString stringWithUTF8String:text.c_str()]];
  std::weak_ptr<Impl> weak = impl_;
  [impl_->task sendMessage:message
         completionHandler:^(NSError* error) {
           if (!error) return;
           std::shared_ptr<Impl> self = weak.lock();
           if (!self || self->closed.load()) return;
           self->open.store(false);
           StateHandler handler;
           {
             std::lock_guard<std::mutex> lock(self->mutex);
             handler = self->onState;
           }
           if (handler) handler(false, error.localizedDescription.UTF8String);
         }];
}

void Socket::close() {
  impl_->closed.store(true);
  {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->onMessage = nullptr;
    impl_->onState = nullptr;
  }
  if (impl_->task) {
    [impl_->task cancelWithCloseCode:NSURLSessionWebSocketCloseCodeNormalClosure reason:nil];
    impl_->task = nil;
  }
  if (impl_->session) {
    [impl_->session invalidateAndCancel];
    impl_->session = nil;
  }
  impl_->open.store(false);
}

bool Socket::open() const { return impl_->open.load(); }

std::string toWebSocketUrl(const std::string& httpUrl) {
  if (core::startsWith(httpUrl, "https://")) return "wss://" + httpUrl.substr(8);
  if (core::startsWith(httpUrl, "http://")) return "ws://" + httpUrl.substr(7);
  return httpUrl;
}

}  // namespace ws
