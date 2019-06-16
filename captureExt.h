#pragma once
#include "typeDefs3D.h"
#include <thread>
#include <iostream>
#include <mutex>
#include <queue>

bool captureExternalDMD();
void captureWindow(int w, int h, int offsetLeft, int offsetTop);

class ThreadPool
{
public:

   ThreadPool(int threads) : shutdown_(false)
   {
      // Create the specified number of threads
      threads_.reserve(threads);
      for (int i = 0; i < threads; ++i)
         threads_.emplace_back(std::bind(&ThreadPool::threadEntry, this, i));
   }

   ~ThreadPool()
   {
      {
         // Unblock any threads and tell them to stop
         std::unique_lock <std::mutex> l(lock_);

         shutdown_ = true;
         condVar_.notify_all();
      }

      // Wait for all threads to stop
      std::cerr << "Joining threads" << std::endl;
      for (auto& thread : threads_)
         thread.join();
   }

   void doJob(std::function <void(void)> func)
   {
      // Place a job on the queu and unblock a thread
      std::unique_lock <std::mutex> l(lock_);

      jobs_.emplace(std::move(func));
      condVar_.notify_one();
   }

protected:

   void threadEntry(int i)
   {
      std::function <void(void)> job;

      while (1)
      {
         {
            std::unique_lock <std::mutex> l(lock_);

            while (!shutdown_ && jobs_.empty())
               condVar_.wait(l);

            if (jobs_.empty())
            {
               // No jobs to do and we are shutting down
               std::cerr << "Thread " << i << " terminates" << std::endl;
               return;
            }

            std::cerr << "Thread " << i << " does a job" << std::endl;
            job = std::move(jobs_.front());
            jobs_.pop();
         }

         // Do the job without holding any locks
         job();
      }

   }

   std::mutex lock_;
   std::condition_variable condVar_;
   bool shutdown_;
   std::queue <std::function <void(void)>> jobs_;
   std::vector <std::thread> threads_;
};