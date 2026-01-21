/*
MIT License

Copyright (c) 2026 H4Group

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

Contact Email: TBD
*/
#include <H4.h>

#ifdef ARDUINO_ARCH_ESP32
	portMUX_TYPE h4_mutex = portMUX_INITIALIZER_UNLOCKED;
	void HAL_enableInterrupts(){ portEXIT_CRITICAL(&h4_mutex); }
	void HAL_disableInterrupts(){ portENTER_CRITICAL(&h4_mutex); }
#else
	void HAL_enableInterrupts(){ interrupts(); }
	void HAL_disableInterrupts(){ noInterrupts();}
#endif
//
//      and ...here we go!
//
void __attribute__((weak)) h4setup(){}
void __attribute__((weak)) h4StartPlugins(){}
void __attribute__((weak)) h4UserLoop(){}
void __attribute__((weak)) onReboot(){} // NOT BEING INVOKED HERE!

H4_TIMER 		    H4::context=nullptr;

void h4reboot(){ h4rebootCore(); }

H4Random::H4Random(uint32_t rmin,uint32_t rmax){ count=task::randomRange(rmin,rmax); }

__attribute__((weak)) H4_INT_MAP h4TaskNames={};

#if H4_COUNT_LOOPS
uint32_t h4Nloops=0;
#endif

#if H4_HOOK_TASKS
	H4_FN_TASK H4::taskHook=[](task* t,uint32_t faze=0){ H4_Pirntf("%s\n",dumpTask(t,faze).data());  };
//    H4_FN_TASK H4::taskHook=[](task* t,uint32_t faze=0){ };

	H4_INT_MAP taskTypes={
		{1,"link"},
		{3,"evry"}, // 3
		{4,"evrn"}, // 4
		{5,"ntim"}, // 5
		{6,"ntrn"}, // 6
		{7,"once"}, // 7
		{8,"1xrn"}, // 8
		{9,"qfun"}, // 9
		{10,"rntx"}, // 10
		{11,"rnrn"}, // 11
		{12,"rptw"}, // 12
		{13,"rpwe"},
		{14,"work"}
	};
	const char* taskPhase[]={"IDL","NEW","SCH","OOQ","DIE"};

void H4::addTaskNames(H4_INT_MAP names){ h4TaskNames.insert(names.begin(),names.end()); }
std::string H4::getTaskType(uint32_t e){ return taskTypes.count(e) ? taskTypes[e]:("????"); }
const char* H4::getTaskName(uint32_t id){ return h4TaskNames.count(id) ? h4TaskNames[id].data():"ANON"; }

std::string H4::dumpTask(task* t,uint32_t faze){
//    H4_Pirntf("H4::dumpTask 0x%08x Phase %d\n",t,faze);
	char buf[1024];
	snprintf(buf, 1023, "T=%08u %s: Q=%02d 0x%08x %s/%s %s %10llu(T%+11llu) %10u %10u %10u L=%d",
			 millis(),
			 taskPhase[faze],
			 h4.size(),
			 (void *)t,
			 //        (void*) t->parent,
			 getTaskType(t->uid / 100).data(),
			 getTaskName(t->uid % 100),
			 t->singleton ? "S" : " ",
			 t->at,
			 t->at > millis() ? (uint64_t)((uint64_t)t->at - millis()) : (uint64_t)(millis() - t->at),
			 t->rmin,
			 t->rmax,
			 t->nrq,
			 t->len
			 //        t->userStore.size()
	);

	return std::string(buf);
}

void H4::dumpQ(){
	if(h4.size()){
		H4_Pirntf("           ACT   nQ    Handle   Type/name    Due @tick(T+          )        Min        Max        nRQ Len\n"); 
		std::vector<task*> tlist=h4._copyQ();
		std::sort(tlist.begin(),tlist.end(),[](const task* a, const task* b){ return a->at < b->at; });
		for(auto const& t:tlist) H4_Pirntf("%s\n",dumpTask(t,0).data()); // faze=quiescent
		H4_Pirntf("\n");
	} else H4_Pirntf("QUEUE EMPTY\n");
}
#else
void H4::dumpQ(){}
#endif

uint64_t millis64(){
	static volatile uint64_t overflow        = 0;
	static volatile uint32_t lastSample         = 0;
	static const uint64_t kOverflowIncrement = static_cast<uint64_t>(0x100000000);

	uint64_t overflowSample;
	uint32_t sample;

	// Tracking timer wrap assumes that this function gets called with
	// a period that is less than 1/2 the timer range.
	HAL_disableInterrupts();
	sample = millis();

	if (lastSample > sample)
	{
		overflow = overflow + kOverflowIncrement;
	}

	lastSample     = sample;
	overflowSample = overflow;
	HAL_enableInterrupts();

	return (overflowSample | static_cast<uint64_t>(sample));
}
//
//		task
//
task::task(
	H4_FN_VOID     	_f,
	uint32_t		_m,
	uint32_t		_x,
	H4_FN_COUNT    	_r,
	H4_FN_VOID     	_c,
	uint32_t		_u,
	bool 			_s
	):
  f{_f},
  rmin{_m},
  rmax{_x},
  reaper{_r},
  chain{_c},
  uid{_u},
  singleton{_s}
{
	static uint64_t count=0;
	count++;
	id=count;
	if(_s){
		uint32_t id=_u%100;
		if(h4.singles.count(id)) h4.singles[id]->endK();    
		h4.singles[id]=this;
	}
	schedule();
}

bool task::operator() (const task* lhs, const task* rhs) const { return ((lhs->at>rhs->at) || (lhs->at==rhs->at && lhs->id>rhs->id)) ? true : false; }

void task::operator()(){
	if(harakiri) _destruct(); // for clean exits
	else {
		f();
		if(reaper){ // it's finite
			if(!(reaper())){ // ...and it just ended
				_chain(); // run chain function if there is one
				if((rmin==rmax) && rmin){
					rmin=86400000; // reque in +24 hrs
					rmax=0;
					reaper=nullptr; // and every day after
					requeue();
				} else _destruct();
			} else requeue();
		} else requeue();
	}
}

void task::_chain(){ if(chain) h4.add(chain,0,0,H4Countdown(1),nullptr,uid); } // prevents tag rescaling during the pass

void task::cancelSingleton(uint32_t s){ if(h4.singles.count(s)) h4.singles[s]->endK(); }

uint32_t task::cleardown(uint32_t pass){
	if(singleton){
		uint32_t id=uid%100;
		h4.singles.erase(id);
	}
	return pass;
}

void task::_destruct(){ 
#if H4_HOOK_TASKS
	H4::taskHook(this,4);
#endif
	lastRites();
	if(partial) free(partial);
	delete this;
}
//		The many ways to die... :)
uint32_t task::endF(){
//    H4_Pirntf("ENDF %p\n",this);
	reaper=H4Countdown(1);
	at=0;
	return cleardown(1+nrq);
}

uint32_t task::endU(){
//    H4_Pirntf("ENDU %p\n",this);
	_chain();
	return nrq+endK();
}

uint32_t task::endC(H4_FN_TIF f){
	bool rv=f(this);
	if(rv) return endF();
	return rv;
}

uint32_t task::endK(){
//    H4_Pirntf("ENDK %p\n",this);
	harakiri=true;
	return cleardown(at=0);
}

uint32_t task::randomRange(uint32_t rmin,uint32_t rmax){ return rmax > rmin ? (rand() % (rmax-rmin)) + rmin:rmin; }

void task::requeue(){
	nrq++;
	schedule();
	h4.qt(this);
}

#if USE_MILLIS_64
void task::schedule(){ at= millis64() + randomRange(rmin,rmax); }
#else
void task::schedule(){ at= (uint64_t) millis() + randomRange(rmin,rmax); }
#endif

void task::createPartial(void* d,size_t l){
	partial=malloc(l);
	memcpy(partial,d,l);
	len = l;
}
//
//      H4
//
task* H4::add(H4_FN_VOID _f,uint32_t _m,uint32_t _x,H4_FN_COUNT _r,H4_FN_VOID _c,uint32_t _u,bool _s){
	task* t=new task(_f,_m,_x,_r,_c,_u,_s);
#if H4_HOOK_TASKS
	H4::taskHook(t,1);
#endif
	qt(t);
	return t;
}

uint32_t H4::gpFramed(task* t,H4_FN_RTPTR f){
	uint32_t rv=0;
	if(t){
		HAL_disableInterrupts();
		if(has(t) || (t==H4::context)) rv=f(); // fix bug where context = 0!
		HAL_enableInterrupts();
	}
	return rv;
}

uint32_t H4::endF(task* t){ return gpFramed(t,[=]{ return t->endF(); }); }

uint32_t H4::endU(task* t){ return gpFramed(t,[=]{ return t->endU(); }); }

bool 	 H4::endC(task* t,H4_FN_TIF f){ return gpFramed(t,[=]{ return t->endC(f); }); }

task* 	 H4::endK(task* t){ return reinterpret_cast<task*>(gpFramed(t,[=]{ return t->endK(); })); }

void H4::qt(task* t){
	HAL_disableInterrupts();
	push(t);
	HAL_enableInterrupts();
#if H4_HOOK_TASKS
	H4::taskHook(t,2);
#endif
}
//
extern  void h4setup();

std::vector<task*> H4::_copyQ(){
	std::vector<task*> t;
	HAL_disableInterrupts();
	t=c;
	HAL_enableInterrupts();
	return t;
}

void H4::_hookLoop(H4_FN_VOID f,uint32_t subid){
	if(f) {
		unloadables[subid]=loopChain.size();
		loopChain.push_back(f);
	}
}

bool H4::_unHook(uint32_t subid){
	if(unloadables.count(subid)){
		loopChain.erase(loopChain.begin()+unloadables[subid]);
		unloadables.erase(subid);
		return true;
	}
	return false;
}

#if !USE_MILLIS_64
void H4::rolloverFix()
{
	H4_Pirntf("Fixing rollover\n");
	std::vector<task*> tasks;
	HAL_disableInterrupts(); // why?
	while (!empty()){
		tasks.emplace_back(top());
		pop();
	}
	for (auto &t:tasks){
		if (t->at>UINT32_MAX)
			t->at-=UINT32_MAX;
		
		push(t);
	}
	HAL_enableInterrupts();
	scheduleRollover();
	// dumpQ();
}
void H4::scheduleRollover()
{
	H4_Pirntf("Scheduling Rollover\n");
	h4.once(H4_SAFETY_TIME + 500, []()
			{ h4.once(UINT32_MAX - millis() - H4_SAFETY_TIME, []()
					  { h4.rolloverFix(); }); });
}
#endif

void setup(){
	h4StartPlugins();
	h4.setup();
	h4setup();
}        

void loop(){ 
	h4.loop();
}

void H4::cancelAll(H4_FN_VOID f){
	HAL_disableInterrupts();
	while(!empty()){
		top()->endK();
		pop();
	}
	HAL_enableInterrupts();
	if(f) f();
}

H4_TASK_PTR H4::every(uint32_t msec,H4_FN_VOID fn,H4_FN_VOID fnc,uint32_t u,bool s){ return add(fn,msec,0,nullptr,fnc,TAG(3),s); }

H4_TASK_PTR H4::everyRandom(uint32_t Rmin,uint32_t Rmax,H4_FN_VOID fn,H4_FN_VOID fnc,uint32_t u,bool s){ return add(fn,Rmin,Rmax,nullptr,fnc,TAG(4),s); }

H4_TASK_PTR H4::nTimes(uint32_t n,uint32_t msec,H4_FN_VOID fn,H4_FN_VOID fnc,uint32_t u,bool s){ return add(fn,msec,0,H4Countdown(n),fnc,TAG(5),s); }

H4_TASK_PTR H4::nTimesRandom(uint32_t n,uint32_t Rmin,uint32_t Rmax,H4_FN_VOID fn,H4_FN_VOID fnc,uint32_t u,bool s){ return add(fn,Rmin,Rmax,H4Countdown(n),fnc,TAG(6),s); }

H4_TASK_PTR H4::once(uint32_t msec,H4_FN_VOID fn,H4_FN_VOID fnc,uint32_t u,bool s){ return add(fn,msec,0,H4Countdown(1),fnc,TAG(7),s); }

H4_TASK_PTR H4::onceRandom(uint32_t Rmin,uint32_t Rmax,H4_FN_VOID fn,H4_FN_VOID fnc,uint32_t u,bool s){ return add(fn,Rmin,Rmax,H4Countdown(1),fnc,TAG(8),s); }

H4_TASK_PTR H4::queueFunction(H4_FN_VOID fn,H4_FN_VOID fnc,uint32_t u,bool s){ return add(fn,0,0,H4Countdown(1),fnc,TAG(9),s); }

H4_TASK_PTR H4::randomTimes(uint32_t tmin,uint32_t tmax,uint32_t msec,H4_FN_VOID fn,H4_FN_VOID fnc,uint32_t u,bool s){ return add(fn,msec,0,H4Random(tmin,tmax),fnc,TAG(10),s); }

H4_TASK_PTR H4::randomTimesRandom(uint32_t tmin,uint32_t tmax,uint32_t Rmin,uint32_t Rmax,H4_FN_VOID fn,H4_FN_VOID fnc,uint32_t u,bool s){ return add(fn,Rmin,Rmax,H4Random(tmin,tmax),fnc,TAG(11),s); }

H4_TASK_PTR H4::repeatWhile(H4_FN_COUNT fncd,uint32_t msec,H4_FN_VOID fn,H4_FN_VOID fnc,uint32_t u,bool s){ return add(fn,msec,0,fncd,fnc,TAG(12),s); }

H4_TASK_PTR H4::repeatWhileEver(H4_FN_COUNT fncd,uint32_t msec,H4_FN_VOID fn,H4_FN_VOID fnc,uint32_t u,bool s){
  return add(fn,msec,0,fncd,
				std::bind([this](H4_FN_COUNT fncd,uint32_t msec,H4_FN_VOID fn,H4_FN_VOID fnc,uint32_t u,bool s){
					fnc();
					repeatWhileEver(fncd,msec,fn,fnc,u,s);
				},fncd,msec,fn,fnc,u,s),
			TAG(13),s);
}

void H4::setup(){
#if !USE_MILLIS_64
	scheduleRollover();
#endif
}

void H4::loop(){
	task* t=nullptr;
#if USE_MILLIS_64
	uint64_t now = millis64();
#else
	uint32_t now=(uint32_t) millis(); // can't do inside loop...clocks dont work when HAL_disableInterrupts()!!!
#endif
	HAL_disableInterrupts();
	if(size()){
		if(((int64_t)(top()->at - now)) < 1) {
			t=top();
			pop();
		}
	}
	HAL_enableInterrupts();
	if(t){ // H4P 35000 35100
		H4::context=t;
//        H4_Pirntf("T=%u H4context <-- %p\n",millis(),t);
		(*t)();
//        H4_Pirntf("T=%u H4context --> %p\n",millis(),t);
//        dumpQ();
	};
//
	for(auto const &f:loopChain) f();
#if H4_USERLOOP
	h4UserLoop();
#endif
#if H4_COUNT_LOOPS
	h4Nloops++;
#endif
}