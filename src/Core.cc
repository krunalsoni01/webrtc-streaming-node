/*
* The MIT License (MIT)
*
* Copyright (c) 2015 vmolsa <ville.molsa@gmail.com> (http://github.com/vmolsa)
*
* Permission is hereby granted, free of charge, to any person obtaining a copy
* of this software and associated documentation files (the "Software"), to deal
* in the Software without restriction, including without limitation the rights
* to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
* copies of the Software, and to permit persons to whom the Software is
* furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in
* all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
* AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
* OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
* THE SOFTWARE.
*
*/

#include <nan.h>
#include "Core.h"

#include "talk/app/webrtc/peerconnectionfactoryproxy.h"
#include "talk/app/webrtc/proxy.h"

using namespace v8;
using namespace WebRTC;

class BlockingThread : public rtc::Thread {
  public:
    virtual void Run() {
      LOG(LS_INFO) << __PRETTY_FUNCTION__;
      
      rtc::Thread::SetAllowBlockingCalls(true);
      rtc::Thread::Run();
    }
};

class ThreadPool {
  public:
    static void Init() {
      LOG(LS_INFO) << __PRETTY_FUNCTION__;

      _instances = 4; // TODO(): Get cpu count
      _pool = new ThreadPool[_instances];
    }
    
    static void Dispose() {
      LOG(LS_INFO) << __PRETTY_FUNCTION__;
      
      delete [] _pool;
    }
  
    static ThreadPool *GetPool() {
      LOG(LS_INFO) << __PRETTY_FUNCTION__;
      
      int selected = _instances - 1;
      size_t count = _pool[_instances - 1]._count;
      
      for (int index = 0; index < _instances; index++) {
        if (count >= _pool[index]._count) {
          selected = index;
          count = _pool[index]._count;
        }
      }
      
      return &_pool[selected];
    }
  
    rtc::Thread *GetWorker() {
      LOG(LS_INFO) << __PRETTY_FUNCTION__;
      
      return static_cast<rtc::Thread*>(_worker);
    }
    
    void Inc() {
      _count++;
    }
    
    void Dec() {
      _count--;
    }
    
  private:
    ThreadPool() : _count(0), _worker(new BlockingThread()) {
      LOG(LS_INFO) << __PRETTY_FUNCTION__;
      
      _worker->Start();
    }
    
    virtual ~ThreadPool() {
      LOG(LS_INFO) << __PRETTY_FUNCTION__;
      
      _worker->Stop();
      delete _worker;
    }
    
  protected:
    size_t _count;
    BlockingThread* _worker;
    static ThreadPool* _pool;
    static int _instances;
};

ThreadPool* ThreadPool::_pool;
int ThreadPool::_instances;

class ThreadConstructor {
  public:
    ThreadConstructor() : _pool(ThreadPool::GetPool()) {
      LOG(LS_INFO) << __PRETTY_FUNCTION__;
      
      _pool->Inc();
    }
    
    virtual ~ThreadConstructor() {
      LOG(LS_INFO) << __PRETTY_FUNCTION__;
      
      _pool->Dec();
    }
    
    rtc::Thread *Current() const {
      LOG(LS_INFO) << __PRETTY_FUNCTION__;
      
      return _pool->GetWorker();
    }
     
  protected:
    ThreadPool* _pool;
};

class PeerConnectionFactory : public ThreadConstructor, public webrtc::PeerConnectionFactory {
  public:
    PeerConnectionFactory() :
      webrtc::PeerConnectionFactory(rtc::Thread::Current(), ThreadConstructor::Current(), NULL, NULL, NULL)
    {
      LOG(LS_INFO) << __PRETTY_FUNCTION__;
    }    
};

BlockingThread* _signal;
rtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface> _factory;
rtc::scoped_ptr<cricket::DeviceManagerInterface> _manager;

void Core::Init() {
  LOG(LS_INFO) << __PRETTY_FUNCTION__;
  rtc::InitializeSSL();

  _signal = new BlockingThread();
  _signal->Start();

  rtc::ThreadManager::Instance()->SetCurrentThread(_signal);
  
  if (rtc::ThreadManager::Instance()->CurrentThread() != _signal) {
    LOG(LS_ERROR) << "Internal Thread Error!";
    abort();
  }

  ThreadPool::Init();

  _factory = Core::CreateFactory();
  _manager.reset(cricket::DeviceManagerFactory::Create());

  if (!_manager->Init()) {
    _manager.release();
  }
}

void Core::Dispose() {
  LOG(LS_INFO) << __PRETTY_FUNCTION__;
  
  Nan::LowMemoryNotification();
  
  _factory.release();

  if (_manager.get()) {
    _manager->Terminate();
  }

  _manager.release();
  
  ThreadPool::Dispose();
  
  _signal->Stop(); 
  delete _signal;
}

rtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface> Core::CreateFactory() {
  rtc::scoped_refptr<PeerConnectionFactory> factory(new rtc::RefCountedObject<PeerConnectionFactory>());
  
  webrtc::MethodCall0<PeerConnectionFactory, bool> call(factory.get(), &webrtc::PeerConnectionFactory::Initialize);
  bool result = call.Marshal(factory->signaling_thread());
  
  if (!result) {
    return NULL;
  }
  
  return webrtc::PeerConnectionFactoryProxy::Create(factory->signaling_thread(), factory);
}

webrtc::PeerConnectionFactoryInterface* Core::GetFactory() {
  LOG(LS_INFO) << __PRETTY_FUNCTION__;
  
  return _factory.get();
}

cricket::DeviceManagerInterface* Core::GetManager() {
  LOG(LS_INFO) << __PRETTY_FUNCTION__;
  
  return _manager.get();
}