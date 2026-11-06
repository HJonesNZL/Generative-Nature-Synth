#include <atlas/core.hpp>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <new>
std::atomic<std::size_t> allocationCount{};
void* operator new(std::size_t n) { allocationCount.fetch_add(1,std::memory_order_relaxed); if(auto p=std::malloc(n?n:1))return p; throw std::bad_alloc{}; }
void* operator new[](std::size_t n){return ::operator new(n);}
void operator delete(void* p)noexcept{std::free(p);} void operator delete[](void* p)noexcept{std::free(p);}
void operator delete(void* p,std::size_t)noexcept{std::free(p);} void operator delete[](void* p,std::size_t)noexcept{std::free(p);}
atlas::Event event(std::int64_t seq, atlas::Microseconds time){atlas::Event e; e.id="birdweather:provider-unique-long-id-"+std::to_string(seq);e.sourceId="birdweather";e.kind="acoustic_species_detection";e.locationKind="station_detection";e.revisionSequence=seq;e.observedAt=time;return e;}
int main(){
 {atlas::Timeline t({event(1,0),event(2,500),event(3,1000),event(4,1500)},atlas::TimeBasis::observed); for(unsigned size:{32U,64U}){std::cout<<"partition="<<size<<" accepted=";std::size_t omitted=0;for(std::int64_t frame=0;frame<128;frame+=size){std::array<atlas::BlockTrigger,2> out{};auto r=t.block(frame,size,48000,out);omitted+=r.omitted;for(std::size_t i=0;i<r.written;++i)std::cout<<out[i].revisionSequence<<",";}std::cout<<" omitted="<<omitted<<"\n";}}
 for(int run=0;run<3;++run){std::vector<atlas::Event> v;v.reserve(50000);std::uint32_t seed=3;for(std::int64_t i=0;i<50000;++i){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;v.push_back(event(i+1,seed));}const auto before=allocationCount.load();const auto start=std::chrono::steady_clock::now();atlas::Timeline t(std::move(v),atlas::TimeBasis::observed);const auto finish=std::chrono::steady_clock::now();std::cout<<"50000_events run="<<run<<" construct_allocations="<<allocationCount.load()-before<<" elapsed_ms="<<std::chrono::duration<double,std::milli>(finish-start).count()<<"\n";}
}
