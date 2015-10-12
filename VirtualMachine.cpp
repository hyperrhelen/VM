// VirtualMachine.cpp
// Helen Chac & Nathan Truong
// ECS 150 SQ 2015
// Prof: Nitta


#include "VirtualMachine.h"
#include "Machine.h"
#include <unistd.h>
#include <cstdio>
#include <cstdlib>
#include <stdint.h>
#include <list>
#include <vector>
#include <queue>
#include <iostream>
#include <cstring>
#include <string>
#include <fcntl.h>
#include <iomanip>
using namespace std;


extern "C"{
	// Stuff for memory.
	typedef unsigned int TVMMemoryPoolID, *TVMMemoryPoolIDRef;
	extern const TVMMemoryPoolID VM_MEMORY_POOL_ID_SYSTEM = 0;


	#define VM_THREAD_PRIORITY_IDLE ((TVMThreadPriority)0x00)
	#define MAX_SIZE 64
	#define SHARED 1
	#define MAX_READ_IN 512
	//This is the struct that holds the threadControlBlock
	class threadControlBlock{
	public:
		TVMThreadIDRef refThreadID;
		TVMThreadID threadID;
		TVMThreadState currState;
		TVMThreadPriority currPrio;
		TVMMemorySize memSize;
		uint8_t *basePtrStack;
		TVMThreadEntry entryFcn;
		void *entryParam;
		SMachineContext *currContext;
		TVMTick numTicks;
		int fileResult;
		TVMMutexID mutexNum;

		void* fileData;
		int fileLength;
		/*
			Possibly hold a pointer of ID of a mutex
			Possibly holding a list of held mutexes
		*/
		threadControlBlock(){
			mutexNum = -1;
			currContext = new SMachineContext();
			entryParam = NULL;
			numTicks = 0;
			fileResult = 0;
		}
		threadControlBlock(TVMThreadIDRef refThread, TVMThreadID tid, 
			TVMThreadState state, TVMThreadPriority priority, TVMThreadEntry enter, 
			void *param, TVMMemorySize mem){
			refThreadID = refThread;
			*refThreadID = tid;
			threadID = *refThreadID;
			currState = state;
			currPrio = priority;
			//enter = entryFcn;
			entryFcn = enter;
			entryParam = param;
			memSize = mem;
			currContext = new SMachineContext();
			numTicks = 0;
			mutexNum = -1;
			fileResult = 0;
		}
	}; //threadControlBlock


	class mutex{
	public:
		TVMMutexIDRef muxID;
		TVMThreadID owner;
		// if the owner is NULL, then it's unlocked.
		int lock; // 0 for locked
		// waiting for the acquired block
		queue<threadControlBlock*> waitingQ[3];
		mutex(){
			lock = 0;
			muxID = NULL;
			owner = 0;
		}
	};


	typedef struct {
		uint8_t* baseAddr;
		TVMMemorySize length;
	} memoryChunk;

	/*
		Below, I have created a global variable called
		memoryPoolList that will store the memory pools
	*/
	class memoryPool{
	public:
		uint8_t* base;
		TVMMemoryPoolID poolID;
		TVMMemorySize size;
		TVMMemorySize availSPace;
		list<memoryChunk> freeSpace;
		list<memoryChunk> allocatedSpace;
	
		memoryPool(){

		}
		
		//constructor for the memory pool
		memoryPool(void *myBase, TVMMemorySize newSize, TVMMemoryPoolIDRef refMemory){
			poolID = *refMemory;
			base = (uint8_t *) myBase;
			size = newSize;
			availSPace = size;
			// Memory Chunk declarations
			memoryChunk chunk;
			chunk.baseAddr = base;
			chunk.length = availSPace;
			freeSpace.push_back(chunk);
			// Also need to keep track of free spaces, sizes, and 
			// allocated spaces
			
		}
		~memoryPool(){
			
			// Iterate through all the allocated Spaces and remove it.
			
			for(int index = 0; index < (int)allocatedSpace.size(); index++){

			}
			for(int index = 0; index < (int)freeSpace.size(); index++){

			}

		} // deconstructor

		//memoryPool::
		void allocate(uint8_t** ptr, TVMMemorySize newSize){
			/*
				You can allocate space because you found some! 
				They pass you the pointer, and we're supposed to 
				allocate space in that location. 
				You want to keep this in a list of allocated space stack
			*/

			memoryChunk tempChunk;
			tempChunk.length = newSize;
			tempChunk.baseAddr = *ptr;
			allocatedSpace.push_back(tempChunk);

			/*
				Use the pointer and find the difference between that and the base
				stack pointer. Then you want to also add it to store it into your list
			*/
		}

	};

	typedef struct{
		uint32_t BS_jmpBoot;
		uint8_t BS_OEMName[8];
		uint16_t BPB_BytsPerSec;
		
		uint8_t BPB_SecPerClus;
		uint16_t BPB_RsvdSecCnt;
		uint8_t BPB_NumFATs;
		uint16_t BPB_RootEntCnt;
		uint16_t BPB_TotSec16;
		uint8_t BPB_Media;
		uint16_t BPB_FATSz16;
		uint16_t BPB_SecPerTrk;
		uint16_t BPB_NumHeads;
		uint32_t BPB_HiddSec;
		uint32_t BPB_TotSec32;

		uint8_t BS_DrvNum;
		uint8_t BS_Reserved1;
		uint8_t BS_BootSig;
		uint32_t BS_VolID;
		uint8_t BS_VolLab[11];
		uint8_t BS_FilSysType[8];

		uint16_t firstRootSector;
		uint16_t rootDirectorySectors;
		uint16_t firstDataSector;
		uint16_t clusterCount;

	}bpbStruct;

	typedef struct{
		SVMDirectoryEntry entry;
		uint16_t DIR_FstClusLO;
		uint16_t DIR_FstClusHI;
	}directory;


	typedef struct{
		int clusterID;
		bool dirty;
		uint8_t* data;
	}clusterBlock;

	typedef struct{
		int directoryID;
		uint16_t firstClus;
		uint16_t next;
		SVMDirectoryEntry directoryEntry;
	}openDir;



	//Globally defined variables.
	//We want to have High, Medium and Low priority queues
	queue<threadControlBlock*> highQ;
	queue<threadControlBlock*> mediumQ;
	queue<threadControlBlock*> lowQ;
	vector<threadControlBlock*> sleepList;
	vector<threadControlBlock> threads;
	vector<mutex*> mutexList;

	// This can be for our sharedwaitingQ
	queue<threadControlBlock*> sharedMemWaitQ[3];
	
	vector<memoryPool*> memoryPoolList;

	bpbStruct globalBPB;
	vector<uint16_t> globalFAT;
	vector<SVMDirectoryEntry> globalRoot;
	vector<directory> globalDirectory;
	vector<uint16_t> openFileDesc;
	vector<clusterBlock> globalCluster;
	vector<openDir> openDirList;
	string currDirectory = "/";
	//vector<directory> globalDirRoot;

	volatile TVMThreadID currThread = 0;
	static TMachineAlarmCallback callbackAlarmFcn = NULL;
	/*
		Instantiating the global tick variable to be used
		and made sure it was voltaile so it's able to be accessed
		in other parts other threads
	*/
	volatile int tickCount = 0;
	int mountedFATdesc;
	/*
		PROTOTYPES::
		Since we want to be using a function from VirtualMachineUtils
		we want to create a PROTOTYPE that says that we're using the
		function. There are also some prototypes for functions that
		were created in this file that aren't in the header
	*/

	TVMMainEntry VMLoadModule(const char* module);
	TVMStatus VMFilePrint(int filedescriptor, const char *format, ...);
	void VMUnloadModule(void);
	void scheduler();
	void pushQueue(TVMThreadID);
	void fileCallback(void *param, int result);
	

	/*
		We want to create a callback function which will then decrement
		the number of ticks that we have.
	*/
	void alarmCallback(void *param){
		int i = 0;
		/*
			Make sure that there's something in your sleepList
			before you go through the loop.
			Otherwise, you don't need to.
		*/
		while(1){
			if(i == (int) (sleepList).size()){
				break;
			}
			sleepList[i]->numTicks--;
			if(sleepList[i]->numTicks == 0){
				
				if(sleepList[i]->currPrio == VM_THREAD_PRIORITY_HIGH){
					highQ.push(sleepList[i]);
				}
				else if (sleepList[i]->currPrio == VM_THREAD_PRIORITY_NORMAL){
					mediumQ.push(sleepList[i]);
				}
				else if (sleepList[i]->currPrio == VM_THREAD_PRIORITY_LOW){
					lowQ.push(sleepList[i]);
				}
				
				sleepList.erase((sleepList).begin()+i);
			}
			else{
				i++;
			}
		}
		
		/*
			When we call alarm callback since the quantum is up, we 
			want to switch to the next thread, so we push it into 
			the queue and set it as ready. This way, this gives an
			opportunity for other threads to run.
		*/
		if(threads[currThread].currState == VM_THREAD_STATE_RUNNING){
			threads[currThread].currState = VM_THREAD_STATE_READY;
			pushQueue(currThread);
		}		
		scheduler();
	}

	//runs idle if there is no threads running
	void idleFunction(void *param){
		
		MachineEnableSignals();
		while(1) {}
	}


	/*
		Pushes the thread into the respective queues based off of 
		the priority of the thread into the ready state
	*/
	void pushQueue(TVMThreadID thread){
		threads[thread].currState = VM_THREAD_STATE_READY;
		switch(threads[thread].currPrio){
			case VM_THREAD_PRIORITY_HIGH:
				highQ.push(&threads[thread]);
				break;
			case VM_THREAD_PRIORITY_NORMAL:
				mediumQ.push(&threads[thread]);
				break;
			case VM_THREAD_PRIORITY_LOW:
				lowQ.push(&threads[thread]);
				break;
			default:
				break;
		}
	}

	/*
		This function tells you that you need to schedule the
		Immediate's priority. Inside the parameter is going to
		a parameter that specifies the priority of which thread
		that we will pop.

	*/
	void schedImm(TVMThreadPriority immPrio){
		TVMThreadID oldThread = threads[currThread].threadID;
		threadControlBlock *tempPtr;
		switch(immPrio){
			case VM_THREAD_PRIORITY_HIGH:
				tempPtr = highQ.front();
				highQ.pop();
				break;
			case VM_THREAD_PRIORITY_NORMAL:
				tempPtr = mediumQ.front();
				mediumQ.pop();
				break;
			case VM_THREAD_PRIORITY_LOW:
				tempPtr = lowQ.front();
				lowQ.pop();
				break;
			default:
				tempPtr = &threads[0];
				// This is the idle thread
				break;
		}
		currThread = tempPtr->threadID;
		MachineContextSwitch(threads[oldThread].currContext, threads[currThread].currContext);
	}

	/*
		You need to check to see if it's an idle thread.
		If it's an idle thread, then you want to set it 
		to ready and push it's queue.
		You need to check the priority of the current thread.
		If there's a thread that's of higher priority, then you want to
		set the current thread to ready and then switch to the new thread.
	*/
	void scheduler(){
		TVMThreadPriority tempPrio = threads[currThread].currPrio;
		/*
			We get the current priority of the thread
			ALSO WANT TO CHECK IF THE CURRENT THREAD IS DEAD
		*/

		threadControlBlock *ptr;
		if(!highQ.empty()){
	
			if(threads[currThread].currState == VM_THREAD_STATE_RUNNING){
				if(tempPrio >= VM_THREAD_PRIORITY_HIGH){
					return;
				}
				else{
					threads[currThread].currState = VM_THREAD_STATE_READY;
					pushQueue(currThread);
				}
			}
			ptr = highQ.front();
			highQ.pop();
		}
		else if(!mediumQ.empty()){

			if(threads[currThread].currState == VM_THREAD_STATE_RUNNING){
				if(tempPrio >= VM_THREAD_PRIORITY_NORMAL){
				
					return;
				}
				else{
					threads[currThread].currState = VM_THREAD_STATE_READY;
					pushQueue(currThread);
				}
			}
			ptr = mediumQ.front();
			mediumQ.pop();
		}
		else if(!lowQ.empty()){

			if(threads[currThread].currState == VM_THREAD_STATE_RUNNING){

				if(tempPrio >= VM_THREAD_PRIORITY_LOW){
					return;
				}
				else{
					threads[currThread].currState = VM_THREAD_STATE_READY;
					pushQueue(currThread);
				}
			}
			ptr = lowQ.front();
			lowQ.pop();
		}
		else{
			if(threads[currThread].currState == VM_THREAD_STATE_RUNNING){
				return;
			} // just needed to add this.
			ptr = &threads[0]; // idle thread
		}

		TVMThreadID oldThread = currThread;
		currThread = ptr->threadID;
		threads[currThread].currState = VM_THREAD_STATE_RUNNING;

		MachineContextSwitch(threads[oldThread].currContext, threads[currThread].currContext);
	}

	void convToTwo(uint16_t* retNew, uint8_t inZero, uint8_t inOne ){
		*retNew = inZero + (((uint16_t)inOne)<<8);
	}

	void convToThree(uint32_t* retNew, uint8_t inZero, uint8_t inOne, uint8_t inTwo){
		*retNew = inZero + (((uint16_t)inOne)<<8) + (((uint32_t)inTwo)<<16);
	}


	void convToFour(uint32_t* retNew, uint8_t inZero, uint8_t inOne, uint8_t inTwo, uint8_t inThree){
		*retNew = inZero + (((uint16_t)inOne)<<8) + (((uint32_t)inTwo)<<16) + (((uint32_t)inThree)<<24);
	}


	/*
		this is going to read in the section of the BPB to be used to 
		calculate the number of sectors. 
		the bpb information is going to be able to be accessed globally.
	*/
	void parseBPB(uint8_t* bpbPtr){
		//cerr << "NAME: " ;
		for(int i = 0; i < 8; i++){
			globalBPB.BS_OEMName[i] = bpbPtr[i+3];
			
		} // since this is an array and we wanted to view everything that was in it, we had to do it 1 by 1
		
		//cerr << globalBPB.BS_OEMName << endl;
		
		convToTwo(&globalBPB.BPB_BytsPerSec, bpbPtr[11], bpbPtr[12]);
		//cerr << "bytesPerSec:" <<  globalBPB.BPB_BytsPerSec << endl;
		
		globalBPB.BPB_SecPerClus = bpbPtr[13];
		//cerr << "Sec Per Clus: " << (int)globalBPB.BPB_SecPerClus << endl;

		convToTwo(&globalBPB.BPB_RsvdSecCnt, bpbPtr[14], bpbPtr[15]);
		//cerr << "BPB_RsvdSecCnt: " << (int)globalBPB.BPB_RsvdSecCnt << endl;
		
		globalBPB.BPB_NumFATs = bpbPtr[16];
		//cerr << "NumFats: " << (int)globalBPB.BPB_NumFATs << endl;
		
		convToTwo(&globalBPB.BPB_RootEntCnt, bpbPtr[17], bpbPtr[18]);
		//cerr << "ROOT Count: " << (int)globalBPB.BPB_RootEntCnt << endl;

		convToTwo(&globalBPB.BPB_TotSec16, bpbPtr[19], bpbPtr[20]);
		//cerr << "TOTSEC16: " << (int) globalBPB.BPB_TotSec16 << endl;
		
		globalBPB.BPB_Media = bpbPtr[21];
		//cerr << "BPBMedia: " << (int) globalBPB.BPB_Media << endl;

		convToTwo(&globalBPB.BPB_FATSz16, bpbPtr[22], bpbPtr[23]);
		//cerr << "FATsz16: " << (int) globalBPB.BPB_FATSz16 << endl;
		
		
		convToTwo(&globalBPB.BPB_SecPerTrk, bpbPtr[24], bpbPtr[25]);
		//cerr << "SecPerTrack: " << (int) globalBPB.BPB_SecPerTrk << endl;
		
		convToTwo(&globalBPB.BPB_NumHeads, bpbPtr[26], bpbPtr[27]);
		//cerr << "Num Heads : " << (int) globalBPB.BPB_NumHeads << endl;
		
		convToFour(&globalBPB.BPB_HiddSec, bpbPtr[28], bpbPtr[29], bpbPtr[30], bpbPtr[31]);
		//cerr << "HIDD SEC: " << (uint32_t) globalBPB.BPB_HiddSec<< endl;
		
		convToFour(&globalBPB.BPB_TotSec32, bpbPtr[32], bpbPtr[33], bpbPtr[34], bpbPtr[35]);
		//cerr << "TOTSEC32: " << (uint32_t) globalBPB.BPB_TotSec32 << endl;
		
		globalBPB.BS_DrvNum = bpbPtr[36];
		//cerr << "Drive #: " << (int) globalBPB.BS_DrvNum << endl;

		globalBPB.BS_BootSig = bpbPtr[38];
		//cerr << "BOOTSIG: " << (int)globalBPB.BS_BootSig << endl;

		for(int i = 0; i < 11; i++){
			globalBPB.BS_VolLab[i] = bpbPtr[43+i];
		}
		//cerr << "Volume Label: " << '"'<< globalBPB.BS_VolLab << '"' << endl;

		for(int i = 0; i < 8; i++){
			globalBPB.BS_FilSysType[i] = bpbPtr[54+i];
		}
		//cerr << "File System Type: '" << globalBPB.BS_FilSysType << "'" << endl;

		convToFour(&globalBPB.BS_VolID, bpbPtr[39], bpbPtr[40], bpbPtr[41], bpbPtr[42]);
		//cerr << "VOL ID: " << hex << globalBPB.BS_VolID << dec << endl;
		
		globalBPB.firstRootSector = globalBPB.BPB_RsvdSecCnt + globalBPB.BPB_NumFATs*globalBPB.BPB_FATSz16;
		//cerr << "FirstRoot: " << (uint16_t) globalBPB.firstRootSector << endl;

		globalBPB.rootDirectorySectors = (globalBPB.BPB_RootEntCnt*32)/512;
		//cerr << "rootDirectorySectors: " << (uint16_t) globalBPB.rootDirectorySectors << endl;

		globalBPB.firstDataSector = globalBPB.firstRootSector + globalBPB.rootDirectorySectors;
		//cerr << "firstData: " << (uint16_t) globalBPB.firstDataSector << endl;

		globalBPB.clusterCount = (globalBPB.BPB_TotSec32 - globalBPB.firstDataSector) / globalBPB.BPB_SecPerClus;
		//cerr << "clusterCount: " << (uint16_t) globalBPB.clusterCount << endl;
		
	
	}//parseBPB

	void readSector(int index, uint8_t** data, int threadID){
		MachineFileSeek(mountedFATdesc, index, 0, fileCallback, &threads[threadID]);
		threads[threadID].currState = VM_THREAD_STATE_WAITING;
		scheduler();
		VMMemoryPoolAllocate(1, 512, (void**) data);
		MachineFileRead(mountedFATdesc, (void*) *data, 512, fileCallback, &threads[threadID]);
		threads[threadID].currState = VM_THREAD_STATE_WAITING;
		scheduler();
	}

	void writeSector(int index, uint8_t** data, int threadID){
		MachineFileSeek(mountedFATdesc, index, 0, fileCallback, &threads[threadID]);
		threads[threadID].currState = VM_THREAD_STATE_WAITING;
		scheduler();
		MachineFileWrite(mountedFATdesc, (void*) *data, 512, fileCallback, &threads[threadID]);
		threads[threadID].currState = VM_THREAD_STATE_WAITING;
		scheduler();
	}

	void readFATSector(){

		int first = globalBPB.BPB_RsvdSecCnt*512;

		for(int fatCount = 0; fatCount < 1; fatCount++){

			for(int sectorCount = 0; sectorCount < globalBPB.BPB_FATSz16; sectorCount++){
				/*
				MachineFileSeek(mountedFATdesc, first, 0, fileCallback, &threads[1]);
				threads[1].currState = VM_THREAD_STATE_WAITING;
				scheduler();
				*/
				uint8_t* fat;
				/*
				VMMemoryPoolAllocate(1, 512, (void**) &fat);
				MachineFileRead(mountedFATdesc, (void*) fat, 512, fileCallback, &threads[1]);
				threads[1].currState = VM_THREAD_STATE_WAITING;
				scheduler();
				*/
				readSector(first, &fat, 1);
				for(int i = 0; i < 512; i+=2){
					if (i % 16 == 0){
						//cerr << endl;
					}
					uint16_t temp = fat[i] + ((uint16_t)fat[i+1]<<8); 
					//cerr << hex  << setw(4) << setfill('0') << unsigned(temp) << " " ; 
					globalFAT.push_back(temp);
				}	
				first += 512;
				VMMemoryPoolDeallocate(1, fat);
				
			}
			
		}

	}//readSector

	void convTime(unsigned char* hour, unsigned char* minute, unsigned char* second, uint16_t times){
		*hour = (times >> 11);
		*minute = (times>>5) & 0x2F;
		*second = (times & 0x1F) << 1;

	}//converting the Time

	void convDate(unsigned int* year, unsigned char* month, unsigned char* day, uint16_t date){
		*year = (date >> 9) + 1980;
		*month = (date >> 5) & 0xF;
		*day = (date & 0x1F);
	}//converting the date

 
	void readRoot(){
		int begin = globalBPB.firstRootSector*512;

		for(int clusCnt = 0; clusCnt < (globalBPB.BPB_RootEntCnt*32/512); clusCnt++){
			
			MachineFileSeek(mountedFATdesc, begin, 0, fileCallback, &threads[1]);
			threads[1].currState = VM_THREAD_STATE_WAITING;
			scheduler();
			uint8_t* cluster; 
			VMMemoryPoolAllocate(1, 512, (void**) &cluster);
			MachineFileRead(mountedFATdesc, (void*) cluster, 512, fileCallback, &threads[1]);
			threads[1].currState = VM_THREAD_STATE_WAITING;
			scheduler();
			//readSector(begin, &cluster, 1);
			// call a function to read the cluster;
			//directory temp;
			for(int clus = 0; clus < 512/32; clus++){
				//cerr << "here; " << endl;
				SVMDirectoryEntry dirs;
				directory temp;
				int i = 0;
				if (cluster[0+clus*32] == 0x00 ||cluster[0+clus*32] == 0xE5){
					continue;
				}
				for(; i < 8; i++){
					if(cluster[i+clus*32] == ' '){
						dirs.DShortFileName[i] = 0x2E;
						i++;	
						break;
					}
					dirs.DShortFileName[i] = cluster[i+clus*32];
					if(i == 7){
						dirs.DShortFileName[i+1] = 0x2E;
						i+=2;
						break;
					}
				}
				//cerr << endl;
				for(int j = 0; j < 4; j++){
					if(j == 0 && cluster[8 + clus * 32] == ' '){
						dirs.DShortFileName[i-1+j] = '\0';
						break;
					}
					if(j == 3 || cluster[j + clus * 32] == ' '){
						dirs.DShortFileName[i+j] = '\0'; 
						break;
					}
					dirs.DShortFileName[i+j] = cluster[j+8 + clus * 32];
				}
				dirs.DAttributes = cluster[11 + clus * 32];
				
				dirs.DCreate.DHundredth = cluster[13 + clus * 32];
				uint16_t createTime;
				convToTwo(&createTime, cluster[14 + clus * 32], cluster[15 + clus * 32]);
				uint16_t createDate;
				convToTwo(&createDate, cluster[16 + clus * 32], cluster[17 + clus * 32]);
				convDate(&dirs.DCreate.DYear, &dirs.DCreate.DMonth, &dirs.DCreate.DDay, createDate);
				convTime(&dirs.DCreate.DHour, &dirs.DCreate.DMinute, &dirs.DCreate.DSecond, createTime);
				convToFour(&dirs.DSize, cluster[28 + clus * 32], cluster[29 + clus * 32], cluster[30 + clus * 32], cluster[31 + clus * 32]);

				uint16_t accessDate;
				convToTwo(&accessDate, cluster[18 + clus * 32], cluster[19 + clus * 32]);
				convDate(&dirs.DAccess.DYear, &dirs.DAccess.DMonth, &dirs.DAccess.DDay, accessDate);

				uint16_t modifyTime;
				convToTwo(&modifyTime, cluster[22 + clus * 32], cluster[23 + clus * 32]);
				convTime(&dirs.DModify.DHour, &dirs.DModify.DMinute, &dirs.DModify.DSecond, modifyTime);
				
				uint16_t modifyDate;
				convToTwo(&modifyDate, cluster[24 + clus * 32], cluster[25 + clus * 32]);
				convDate(&dirs.DModify.DYear, &dirs.DModify.DMonth, &dirs.DModify.DDay, modifyDate);
				
				convToTwo(&temp.DIR_FstClusLO, cluster[26 + clus * 32], cluster[27+clus *32]);
				convToTwo(&temp.DIR_FstClusHI, cluster[20 + clus * 32], cluster[21 + clus * 32]);

				temp.entry = dirs;

				if(dirs.DAttributes != 15){
					//cerr << temp.entry.DShortFileName << endl;
					//cerr << unsigned(dirs.DCreate.DHour)  << " " << unsigned(dirs.DCreate.DMinute) << " " << unsigned(dirs.DCreate.DSecond)<< endl;
					globalDirectory.push_back(temp);
				}	
			}
			
			begin += 512;
			VMMemoryPoolDeallocate(1, cluster);
		}
	}//readRoot

	void traverseFATChain(int clusterNumber, int numOfTimes){
		for(int clus = clusterNumber; clus < clusterNumber+numOfTimes; clus++){
			// read from the cluster...?
		}
	}//traverses the FAT Chain

	void readCluster(int secOffset, uint8_t** data, int threadID){
		
		int sectorSize = 512;
		int offset = (globalBPB.firstDataSector+2+secOffset) * sectorSize;
		//cerr << "GLOBAL DATA : " << globalBPB.firstDataSector << endl;
		
		vector<uint8_t> tempData;
		uint8_t* shareData;
		*data = new uint8_t[globalBPB.BPB_SecPerClus*512];
		tempData.resize(globalBPB.BPB_SecPerClus*512);
		int off = 0;
		for(int i = 0; i < globalBPB.BPB_SecPerClus; i++){
			
			readSector(offset, &shareData, threadID);	
			cerr << shareData << endl;
			memcpy((uint8_t*) *data+off,shareData, 512);
			VMMemoryPoolDeallocate(1, shareData);
			offset += 512;
		}

	}

	void readClusterChain(uint16_t clus, void** data){ 
		int i = 0;
		while(1){
			uint8_t* readData;
			readSector(clus, &readData, currThread);
			memcpy((uint8_t*) *data + i, readData, globalBPB.BPB_SecPerClus*512);
			VMMemoryPoolDeallocate(1, readData);
			
			if(globalFAT[clus] >= 0xFFF8){

				//do stuff before you break.
				//break, you've reached the end of the chain;
				break;
			}
			clus = globalFAT[clus];
			i+= globalBPB.BPB_SecPerClus*512;
		}
	}

	/*
		VMStart will start the machine by loading the module into the
		system. After we load the machine, we want to call the entry
		point to the function and start it by passing in the parameters
		to the application.
	*/
	TVMStatus VMStart(int tickms, TVMMemorySize heapsize, int machinetickms, TVMMemorySize sharedsize,
								const char *mount, int argc, char *argv[]){
		int maxPerBytes = 4096;
		TVMMainEntry VMMain;
		VMMain = VMLoadModule(argv[0]);

		/*
			This part does the error checking
		 	If we aren't able to open the VM then we want to return false
		*/
		if(VMMain == NULL){
			return VM_STATUS_FAILURE;
		}

		/*
			If there isn't any problems or failures with running the VM
			then we want to do the things on the bottom ...
		*/

		// resizing the sharedMemorySize to multiples of 4096
		sharedsize = ((sharedsize+maxPerBytes-1)/maxPerBytes)*maxPerBytes;
		uint8_t* sharedBaseAddress = (uint8_t*) MachineInitialize(machinetickms, sharedsize);

		callbackAlarmFcn = (TMachineAlarmCallback) alarmCallback;
		MachineRequestAlarm(tickms * 1000, callbackAlarmFcn, NULL);
		
		/*
			Over here we create the memory pools for the mainMemoryPool and the shared MemoryPool
			These are later going to be used when we create new threads - uses the mainMemory
			We create the shared Memory, which is shared with the virtual machine.
			Since it is sharing with the virutal machine, it's only going to be accessed during the
			VMWrite and the VMRead functions (because that's when we pass things onto the machine)

		*/
		uint8_t* memoryBase = new uint8_t[heapsize];
		// we're going to change this to heap size
		// we need to initate the memoryBase before we actually pass into the function
		TVMMemoryPoolID mainMemoryID;
		TVMMemoryPoolID sharedMemoryID;
		// when VMMemoryPoolCreate returns, the value of mainMemID = 0
		VMMemoryPoolCreate(memoryBase, heapsize, &mainMemoryID);
		// when VMMemoryPoolCreate returns, the value of sharedMemID = 1
		VMMemoryPoolCreate(sharedBaseAddress, sharedsize, &sharedMemoryID);


		threadControlBlock *tempBlock;
		for(int i = 0; i < 2 ; i++){
			tempBlock = new threadControlBlock();
			tempBlock->threadID = threads.size();
			tempBlock->refThreadID = &(tempBlock->threadID);

			if(i == 0){
				tempBlock->entryFcn = idleFunction;
				tempBlock->currState = VM_THREAD_STATE_READY;
				tempBlock->currPrio = VM_THREAD_PRIORITY_IDLE;
				uint8_t *stackaddr = new uint8_t[0x100000];
				tempBlock->basePtrStack = stackaddr;
				tempBlock->memSize = 0x100000;
				threads.push_back(*tempBlock);
				//MachineContextCreate(&threads[0].currCont)

				MachineContextCreate((threads[0].currContext), idleFunction, NULL, 
					threads[0].basePtrStack, threads[0].memSize);
				// IDLE
				/*
					If you have no more stuff in your queue, then in your IDLE is
					just waiting for signals.
					You want to create an idle function that's just a while loop
					that suspends the threads and then resumes them back
					and forth.
				*/
			}
			if(i == 1){
				tempBlock->currState = VM_THREAD_STATE_RUNNING;
				tempBlock->currPrio = VM_THREAD_PRIORITY_NORMAL;
				//we don't need a base stack pointer for vmMain
				
				threads.push_back(*tempBlock);
				currThread = 1;
			}
		}
		
		/*
			Given the file name, we want to mount it so that we can read it in other
			parts of the program.
			Need to call machine file open and wait until it returns. In our case, we 
			call schedule so we can block until we have the appropriate value for our file
			Then call the Machine file read, which will read the first sector known as the 
			BPB in which we will need to parse out and read it correctly. 
		*/
		MachineFileOpen(mount, O_RDWR, 0, fileCallback, &threads[1]);
		threads[1].currState = VM_THREAD_STATE_WAITING;
		scheduler();
		mountedFATdesc = threads[1].fileResult;
		uint8_t* bpbPtr;
		VMMemoryPoolAllocate(1, 512, (void**) &bpbPtr);
		MachineFileRead(mountedFATdesc, (void*) bpbPtr, 512, fileCallback, &threads[1]);
		threads[1].currState = VM_THREAD_STATE_WAITING;
		scheduler();
		parseBPB(bpbPtr);
		/*
		for(int i = 0; i < 512; i++){
			if(i %16 == 0){
				cerr << endl;
			}
			cerr << hex << setfill('0') << setw(2) <<  bpbPtr[i] << " ";
		}
		*/
		VMMemoryPoolDeallocate(1, bpbPtr);

		readFATSector();
		readRoot();

		
		//readCluster();

		MachineEnableSignals();

		VMMain(argc,argv);
		MachineTerminate();
		/*
			When we run VMMain and it returns something that's not
			NULL, then we want to show that we have returned success
			fully from VMStart.

			Otherwise, if we get NULL from calling the function,
			then we want to return failure from running the VM.
		*/
		VMUnloadModule();
		return VM_STATUS_SUCCESS;
	}




	/*
		Depending on what the application wants to be have written out,
		we will write out the text to the screen.
		We are already passed in the filedescriptor, data and the length,
		which allows easy access to calling the function write() from
		unistd;
		TODO: Each time the function is called, we want it to "WAIT".

	*/
	TVMStatus VMFileWrite(int filedescriptor, void *data, int *length){

		/*
			We want to ensure that we have valid parameters, thus we need
			to check whether data or lenght is NULL before moving forward.
		*/
		if(data == NULL || length == NULL){
			return VM_STATUS_ERROR_INVALID_PARAMETER;
		}

		/*
			If we have successfuly able to write it out onto the screen,
			we will return VM_STATUS_SUCCESS, otherwise, we will return
			VM_STATUS_FAILURE.
		*/
		TMachineSignalState oldState;
		MachineSuspendSignals(&oldState);
		/*	
			you want to call memorypoolallocate on the shared memory
		*/
		if(filedescriptor < 3){
			void* sharedData;
			int readIn = 0;
			// Loop and check for whether or nto we finished writing the whole string
			while(readIn < *length){

				int wordLen = (*length - readIn) / MAX_READ_IN;

				if(wordLen != 0){
					 //if we haven't quite reached the end and we want to allocate 512 space
					wordLen = 512;
				}
				else{
					// we don't need to allocate 512, so we just allocate the remainer
					wordLen = *length % MAX_READ_IN;
				}
				

				// if we can't allocate any more into the shared memory, then we want to push it into the waitingQ
				if(VMMemoryPoolAllocate(SHARED, wordLen, &sharedData) == VM_STATUS_ERROR_INSUFFICIENT_RESOURCES){
					threads[currThread].currState = VM_THREAD_STATE_WAITING;
					switch(threads[currThread].currPrio){
						case VM_THREAD_PRIORITY_HIGH:
							sharedMemWaitQ[2].push(&threads[currThread]);
							break;
						case VM_THREAD_PRIORITY_NORMAL:
							sharedMemWaitQ[1].push(&threads[currThread]);
							break;
						case VM_THREAD_PRIORITY_LOW:
							sharedMemWaitQ[0].push(&threads[currThread]);
							break;
					}
					
					// calling scheduler will switch off into another thread until it is awoken bc there's space
					scheduler();
					
					// since there is space, we want to allocate the space into memory.
					VMMemoryPoolAllocate(SHARED, wordLen, &sharedData);
				}
				/*
					We want to copy the data into the sharedMemory, since we want to pass in the NEXT part, 
					we will be incrementing the data by how much we read in.
					Initally, we read in 0, so it's just starting off of the base ptr. Later, it will do it
					by how much we read in because we continue to update it in our list.
				*/
				memcpy(sharedData, (char*) data+readIn, wordLen);
				
				// we call the machine to write out the data for us from the shared Memory.
				MachineFileWrite(filedescriptor, sharedData, wordLen, fileCallback, &threads[currThread]);
				threads[currThread].currState = VM_THREAD_STATE_WAITING;

				scheduler();
				// dealllcoate the space that we allocated
				VMMemoryPoolDeallocate(SHARED, sharedData);
				
				// increments the value that we read in.
				readIn += wordLen;

			}

			/*
				Once we finish printing out the word, then we can worry about waking up the next thread
				that needs to perform their function. This is similar to release in mutex.
				We check for the highest priority empty queue and work our way to the lower priorities.
			*/

			threadControlBlock* threadOut;
			if(!sharedMemWaitQ[2].empty()){
				threadOut = sharedMemWaitQ[2].front();
				threads[threadOut->threadID].currState = VM_THREAD_STATE_READY;
				pushQueue(threadOut->threadID);
				sharedMemWaitQ[2].pop();
			}
			else if(!sharedMemWaitQ[1].empty()){
				threadOut = sharedMemWaitQ[1].front();
				threads[threadOut->threadID].currState = VM_THREAD_STATE_READY;
				pushQueue(threadOut->threadID);
				sharedMemWaitQ[1].pop();
			}
			else if (!sharedMemWaitQ[0].empty()){
				threadOut = sharedMemWaitQ[0].front();
				threads[threadOut->threadID].currState = VM_THREAD_STATE_READY;
				pushQueue(threadOut->threadID);
				sharedMemWaitQ[0].pop();
			}
		}
		else{
			int clusterLo = openFileDesc[filedescriptor-3];
			int index = ((clusterLo - 2) * globalBPB.BPB_SecPerClus + globalBPB.firstDataSector)*512;
			for(int j = 0; j < globalBPB.BPB_SecPerClus; j++){
				uint8_t* ptr;
				//readSector(index, &ptr);
				VMMemoryPoolAllocate(1, 512, (void**) &ptr);
				memcpy(ptr, (uint8_t*) data + j * 512, 512);
				writeSector(index, &ptr, currThread);
				// come back to here!!!
				//MachineFileWrite(mountedFATdesc, ptr, 512, fileCallback, &threads[currThread]);
				VMMemoryPoolDeallocate(1, ptr);
				index += 512;
			}


		}
		
		
		MachineResumeSignals(&oldState);
		if(threads[currThread].fileResult < 0){
			//return incorrect
			return VM_STATUS_FAILURE;
		}
		else{
			return VM_STATUS_SUCCESS;	
		}
		
	}


	/*
		This function will continue to decrement the number of
		ticks until it times out.
		It has 3 possible return variables:
			VM_STATUS_SUCCESS, VM_TIMEOUT_INFINITE,
			VM_STATUS_ERROR_INVALID_PARAMETER
		This function sets the global tick, our global tick
		is called tickCounter
	*/
	TVMStatus VMThreadSleep(TVMTick tick){

		TMachineSignalState oldState;

		if(tick == VM_TIMEOUT_INFINITE){
			return VM_STATUS_ERROR_INVALID_PARAMETER;
		}

		MachineSuspendSignals(&oldState);
		MachineEnableSignals();		

		if(tick == VM_TIMEOUT_IMMEDIATE){
			threads[currThread].currState = VM_THREAD_STATE_READY;
			pushQueue(currThread);
			/*
				Want to schedule another function
			*/
			schedImm(threads[currThread].currPrio);
			MachineResumeSignals(&oldState);
			return VM_STATUS_SUCCESS;
			
		}
		else{
			threads[currThread].currState = VM_THREAD_STATE_WAITING;
			threads[currThread].numTicks = tick;
			/*
				You want to set the currThread to the # of ticks
			*/
			sleepList.push_back(&threads[currThread]);

			
			scheduler();		
			/*
				You want to push the currentThread into the vector of sleeps
				Then you want to call scheduler which will call context switch
				but don't forget to put the number of ticks into that thread
				so that when we call the alarmCallback, it can decrement the
				number of ticks

				In your alarm callback function, you want to decrement the
				number of ticks that are in the list and if those that are in
				the list are 0, then you want to remove it from the sleep list.
				Then put them in the appropriate Queue to be "ready" to run.
				(Use an iterator or something)
			*/
			MachineResumeSignals(&oldState);
			return VM_STATUS_SUCCESS;
		}

		
	} // end of VMThreadSleep

	/*
		Puts the thread ID into threadref
	*/
	TVMStatus VMThreadID(TVMThreadIDRef threadref){
		if(threads[currThread].refThreadID != NULL){
			threadref = threads[currThread].refThreadID;
			return VM_STATUS_SUCCESS;
		}
		else{
			return VM_STATUS_ERROR_INVALID_PARAMETER;
		}
	}

	TVMStatus VMThreadState(TVMThreadID thread, TVMThreadStateRef state){
		/*
			Error checking
		*/
		if(thread == VM_THREAD_ID_INVALID){
			/*
				Go through our list of threads and if the search returns 0,
				then we want to call invalid error on this.
			*/
			return VM_STATUS_ERROR_INVALID_ID;
		}

		else if(state == NULL){
			return VM_STATUS_ERROR_INVALID_PARAMETER;
		}
		TMachineSignalState oldState;
		MachineSuspendSignals(&oldState);

		*state = threads[thread].currState;
		MachineResumeSignals(&oldState);
		return VM_STATUS_SUCCESS;
	}


	void skeletonFunction(void *param){
		MachineEnableSignals();
		threadControlBlock *temp = (threadControlBlock*) param;
		temp->entryFcn(temp->entryParam);
		VMThreadTerminate(temp->threadID);
	}

	/*
		TODO: We might move this to another class because we may
		create a THREAD class which will hold all the threads.
			thread1-> thread2 -> thread3 (appending to the end list)

		VMThreadCreate() creates a thread in the virtual machine.
		Every time you create a thread, it is initally in the
		VM_THREAD_STATE_DEAD
	*/
	TVMStatus VMThreadCreate(TVMThreadEntry entry, void *param, 
		TVMMemorySize memsize, TVMThreadPriority prio, TVMThreadIDRef tid){
		/*
			Once we create the thread it will go into the dead state.
			The entry parameter specifies the function
			the parameter is basically what gets passed into the function.
		*/
		if (tid == NULL || entry == NULL){
			return VM_STATUS_ERROR_INVALID_PARAMETER;
		}
		TMachineSignalState oldState;
		MachineSuspendSignals(&oldState);

		/*
			Instantiate all the thread stuff
			by passing it through the constructor.
		*/
		threadControlBlock *tempContext = new threadControlBlock(tid, 
			threads.size(), VM_THREAD_STATE_DEAD, prio, entry, param, memsize);
		uint8_t* stackaddr;// = new uint8_t[memsize];
		VMMemoryPoolAllocate(VM_MEMORY_POOL_ID_SYSTEM, memsize, (void**) &stackaddr);

		tempContext->basePtrStack = stackaddr;
		threads.push_back(*tempContext);

		MachineResumeSignals(&oldState);
		return VM_STATUS_SUCCESS;
	}

	/*
		Activates a dead thread in the virtual machine
		deadState -> readyState
		You want to call MachineSwitchContext /
		MachineContextCreate
	*/
	TVMStatus VMThreadActivate(TVMThreadID thread){
		/*
			Run a loop that goes through all the threads
			and make sure that there are threads inside
			the vector.

			TODO: You want to remove the thread from the deadstate
			and put it into the ready state.
		*/
		if(thread == VM_THREAD_ID_INVALID){
			/*
				Go through our list of threads and if the search returns 0,
				then we want to call invalid error on this.
			*/
			return VM_STATUS_ERROR_INVALID_ID;
		}
		/*
			If there is a thread, then we want to call MachineContext Switch
			which then switches the context so that we work run a certain
			thread instead.
		*/
		TMachineSignalState oldState;
		MachineSuspendSignals(&oldState);
		MachineContextCreate((threads[thread].currContext), skeletonFunction,
			&threads[thread], threads[thread].basePtrStack, threads[thread].memSize);
		threads[thread].currState = VM_THREAD_STATE_READY;

		
		//Pushes into the priority queues
		pushQueue(thread);
		/*
			You call the scheduler function which schedules
			the next thread that should be run.
		*/
		scheduler();
		MachineResumeSignals(&oldState);
		return VM_STATUS_SUCCESS;
	}

	//TODO: After the thread dies, it releases any mutexes that holds
	TVMStatus VMThreadTerminate(TVMThreadID thread){
		TMachineSignalState oldState;
		MachineSuspendSignals(&oldState);
		threads[thread].currState = VM_THREAD_STATE_DEAD;
		
		/*
		if(threads[thread].mutexNum < mutexList.size() && threads[thread].mutexNum >= 0){
			if(mutexList[threads[thread].mutexNum]->owner == thread){
				VMMutexRelease(threads[thread].mutexNum);
			}

		}
		*/
		
		//if(thread == currThread){
			scheduler();
		//}
		//scheduler();
		MachineResumeSignals(&oldState);
		return VM_STATUS_SUCCESS;
	}

	/*
		Deletes the specific thread from the virtual machine
	*/
	TVMStatus VMThreadDelete(TVMThreadID thread){
		TMachineSignalState oldState;
		MachineSuspendSignals(&oldState);
		if(thread >  threads.size()){
			MachineResumeSignals(&oldState);
			return VM_STATUS_ERROR_INVALID_ID;
		}
		else if(threads[thread].currState == VM_THREAD_STATE_DEAD){
			// sets the location to NULL
			delete(threads[thread].basePtrStack);
			threads[thread].basePtrStack = NULL;
			threads.erase(threads.begin() + thread);
			MachineResumeSignals(&oldState);
			return VM_STATUS_SUCCESS;
		}
		else{
			MachineResumeSignals(&oldState);
			return VM_STATUS_ERROR_INVALID_STATE;
		}
		return VM_STATUS_SUCCESS;

	}
	/*
		Put the current thread in waiting, then
		we want to call scheduler to switch threads,
		then we want to wake up to do the "read" 
		When we put it in waiting, we want to call the machine
		to wake up and do the read
		Once it finishes, it notifies that it finishes (sends signals)
		and then it goes back to sleep. o_0
		call the callback ready thread, switch thread, then read completes.
	*/
	void fileCallback(void *param, int result){
		threadControlBlock *tempThread = (threadControlBlock*) param;
		tempThread->currState = VM_THREAD_STATE_READY;
		pushQueue(tempThread->threadID);
		threads[tempThread->threadID].fileResult = result;
		//memcopy
		//threads[tempThread->threadID].data
		scheduler();
	}

	// copy & paste from VMFILEWRITE
	TVMStatus VMFileOpen(const char *filename, int flags, int mode, int *filedescriptor){
		if(filedescriptor == NULL || filename == NULL){
			return VM_STATUS_ERROR_INVALID_PARAMETER;
		}
		TMachineSignalState oldState;
		MachineSuspendSignals(&oldState);
		int i = 0;
		char temp[strlen(filename)];// = filename;
		strcpy(temp, filename);
		for(; i < (int) globalDirectory.size(); i++){
			for(int length = 0; length < (int) strlen(temp); length++){
				temp[length] = toupper(temp[length]);
			}
			if(strcmp(temp, globalDirectory[i].entry.DShortFileName) == 0){
				//break, we found it! 
				MachineFileOpen(filename, flags, mode, fileCallback, &threads[currThread]);
				threads[currThread].currState = VM_THREAD_STATE_WAITING;
				scheduler();

				openFileDesc.push_back(globalDirectory[i].DIR_FstClusLO);
				break;
			}
		}
		if(i == (int) globalDirectory.size()){
			clusterBlock tempCluster;
			for(int countFat = 0; countFat < globalBPB.BPB_NumFATs; countFat++){
				if(globalFAT[countFat] == 0){
					// then we found a free one.
					// assign it the value of the FAT.
					tempCluster.clusterID = countFat;
					tempCluster.dirty = false;
					globalCluster.push_back(tempCluster);
					globalFAT[countFat] = 0xFFF8;
					openFileDesc.push_back(countFat);
					break;
				}
				// look for an empty cluster
			}
			// then we couldn't find it.
			/*
				Then you want to allocate a cluster for it.

			*/


			// call machine file open which will halt/block
			/*
			MachineFileOpen(filename, flags, mode, fileCallback, &threads[currThread]);
			threads[currThread].currState = VM_THREAD_STATE_WAITING;
			scheduler();

			if(threads[currThread].fileResult < 0){
				return VM_STATUS_FAILURE;
			}
			else{
				*filedescriptor = threads[currThread].fileResult;
				return VM_STATUS_SUCCESS;	
			}*/
		}
		
		// allocate space in a cluster
	
		/*
			Store the return of the file into the the TCB of the
			current thread
		*/
		MachineResumeSignals(&oldState);
		*filedescriptor = 3 + openFileDesc.size() -1;
		//openFileDesc.push_back(*filedescriptor);
		return VM_STATUS_SUCCESS;

	}

	// copy & paste
	TVMStatus VMFileClose(int filedescriptor){

		TMachineSignalState oldState;
		MachineSuspendSignals(&oldState);
		if(filedescriptor > 3){
			// come fix this later.
			//openFileDesc[filedescriptor-3] = NULL;
			return VM_STATUS_SUCCESS;
		}
		else{
			MachineFileClose(filedescriptor, fileCallback, &threads[currThread]);
			threads[currThread].currState = VM_THREAD_STATE_WAITING;
			scheduler();
			MachineResumeSignals(&oldState);	
			if(threads[currThread].fileResult < 0){
				return VM_STATUS_FAILURE;
			}
			else{
				return VM_STATUS_SUCCESS;
			}
		}
		
		
	}

	// copy & paste
	TVMStatus VMFileRead(int filedescriptor, void *data, int *length){
		if(data == NULL || length == NULL){
			return VM_STATUS_ERROR_INVALID_PARAMETER;
		}
		TMachineSignalState oldState;
		MachineSuspendSignals(&oldState);
		int total = 0;
		if(filedescriptor < 3){

			void* sharedData;

			//int total = 0;
			int readIn = 0;
			// while we haven't finished reading
			while(readIn < *length){

				int wordLen = (*length - readIn) / MAX_READ_IN;

				//checks for whether or not we need to allocate 512 or something else
				if(wordLen != 0){
					// we haven't quite reached the end so we're allocating 512
					wordLen = MAX_READ_IN;
				}
				else{
					// we just want to allocate the remainder space
					wordLen = *length % MAX_READ_IN;
				}

				// checks whether or not we can allocate the space, if we can allcoate the space, then we move on
				// if we can't allocate space, then we push it into respective queues based off of priority.
				if(VMMemoryPoolAllocate(SHARED, wordLen, &sharedData) == VM_STATUS_ERROR_INSUFFICIENT_RESOURCES){
					threads[currThread].currState = VM_THREAD_STATE_WAITING;
					switch(threads[currThread].currPrio){
						case VM_THREAD_PRIORITY_HIGH:
							sharedMemWaitQ[2].push(&threads[currThread]);
							break;
						case VM_THREAD_PRIORITY_NORMAL:
							sharedMemWaitQ[1].push(&threads[currThread]);
							break;
						case VM_THREAD_PRIORITY_LOW:
							sharedMemWaitQ[0].push(&threads[currThread]);
							break;
					}
					scheduler();
					// it will call allocate when it needs to wake up.
					VMMemoryPoolAllocate(SHARED, wordLen, &sharedData);

				}
				// if can allocate, then it will call read and pass the data and put it into the sharedData
				MachineFileRead(filedescriptor, sharedData, wordLen, fileCallback, &threads[currThread]);
				threads[currThread].currState = VM_THREAD_STATE_WAITING;
				scheduler();
				/*
					When it wakes up, it wants to cpy what it's read in, into the correct location in dataa
					Since data is at the base, we want to increment to a different address part
					therefore, we wnat to move it to the next READIN to copy the data into.
				*/
				memcpy((char*)data + readIn, sharedData, wordLen);
				// increment the total number of characters that we had to read and put the into *length later
				total += threads[currThread].fileResult;
				readIn += wordLen;
				VMMemoryPoolDeallocate(SHARED, sharedData);
				

			
			}

			//wakes up the next free one.
			threadControlBlock* threadOut;
			if(!sharedMemWaitQ[2].empty()){
				threadOut = sharedMemWaitQ[2].front();
				threads[threadOut->threadID].currState = VM_THREAD_STATE_READY;
				pushQueue(threadOut->threadID);
				sharedMemWaitQ[2].pop();
			}
			else if(!sharedMemWaitQ[1].empty()){
				threadOut = sharedMemWaitQ[1].front();
				threads[threadOut->threadID].currState = VM_THREAD_STATE_READY;
				pushQueue(threadOut->threadID);
				sharedMemWaitQ[1].pop();
			}
			else if (!sharedMemWaitQ[0].empty()){
				threadOut = sharedMemWaitQ[0].front();
				threads[threadOut->threadID].currState = VM_THREAD_STATE_READY;
				pushQueue(threadOut->threadID);
				sharedMemWaitQ[0].pop();
			}
			
		}
		else{
			/*
			uint16_t clusNum = openFileDesc[filedescriptor-3];
			readClusterChain(clusNum, &data);*/
			// the file descriptor will tell you where in the clusters you want to read from.
			//int clusNum = openFileDesc[filedescriptor - 3];
			//int offset = clusNum * globalBPB.BPB_SecPerClus 
			/*
				Want to find the next cluster, each cluster has another sector;

			*/
			// we want to try to read it from the mounted file system.
			// given the file descriptor, we can check where it is in the cluster
			// the file descriptor, gives us an idea of which clusterID it's in.
		}
		


		MachineResumeSignals(&oldState);
		if(threads[currThread].fileResult < 0){
			return VM_STATUS_FAILURE;
		}
		else{
			*length = total;
			//*length = threads[currThread].fileResult; 
			return VM_STATUS_SUCCESS;
		}
	}

	// copy & paste
	TVMStatus VMFileSeek(int filedescriptor, int offset, int whence, int *newoffset){
		TMachineSignalState oldState;
		MachineSuspendSignals(&oldState);
		
		MachineFileSeek(filedescriptor, offset, whence, fileCallback, &threads[currThread]);
		threads[currThread].currState = VM_THREAD_STATE_WAITING;
		scheduler();
		*newoffset = threads[currThread].fileResult; 
		MachineResumeSignals(&oldState);
		if(threads[currThread].fileResult < 0){
			return VM_STATUS_FAILURE;
		}
		else{
			return VM_STATUS_SUCCESS;
		}
	}

	// create a new mutex ( list of mutex locks )
	TVMStatus VMMutexCreate(TVMMutexIDRef mutexref){
		
		if(mutexref == NULL){
			return VM_STATUS_ERROR_INVALID_PARAMETER;
		}
		TMachineSignalState oldState;
		MachineSuspendSignals(&oldState);

		mutex *tempMutex = new mutex();
		*mutexref = mutexList.size();
		tempMutex->muxID = mutexref;
		mutexList.push_back(tempMutex);

		MachineResumeSignals(&oldState);
		return VM_STATUS_SUCCESS;
	}

	/*
		Function that's created to make it easier and nicer to push
		it into a wait queue inside the mutex
	*/
	void pushWaitQueue(TVMMutexID mutex, TVMThreadID thread){
		threads[thread].currState = VM_THREAD_STATE_WAITING;
		switch(threads[thread].currPrio){
			case VM_THREAD_PRIORITY_HIGH:
				mutexList[mutex]->waitingQ[2].push(&threads[thread]);
				break;
			case VM_THREAD_PRIORITY_NORMAL:
				mutexList[mutex]->waitingQ[1].push(&threads[thread]);
				break;
			case VM_THREAD_PRIORITY_LOW:
				mutexList[mutex]->waitingQ[0].push(&threads[thread]);
				break;
			default:
				break;
		}
	}

	// locks the mutex
	TVMStatus VMMutexAcquire(TVMMutexID mutex, TVMTick timeout){
		TMachineSignalState oldState;
		MachineSuspendSignals(&oldState);
		if(mutex >= mutexList.size()){
			MachineResumeSignals(&oldState);
			return VM_STATUS_ERROR_INVALID_ID;
		}
		if(timeout == VM_TIMEOUT_IMMEDIATE){
			if(mutexList[mutex]->lock == 1){
				MachineResumeSignals(&oldState);
				return VM_STATUS_FAILURE;
			}
			else{
				mutexList[mutex]->lock = 1;
				mutexList[mutex]->owner = currThread;
				threads[currThread].mutexNum = mutex;
				threads[currThread].currState = VM_THREAD_STATE_READY;
				pushQueue(currThread);
			}
		}
		else{
			//unlocked
			if(mutexList[mutex]->lock == 0){
				mutexList[mutex]->lock = 1;
				mutexList[mutex]->owner = currThread;
				threads[currThread].mutexNum = mutex;
				threads[currThread].currState = VM_THREAD_STATE_READY;
				pushQueue(currThread);
			}
			//locked
			else{
				threads[currThread].currState = VM_THREAD_STATE_WAITING;
				if(timeout != VM_TIMEOUT_INFINITE){
					threads[currThread].numTicks = timeout;	
					sleepList.push_back(&threads[currThread]);
				}
				pushWaitQueue(mutex, currThread);

			}
		}
		scheduler();
		MachineResumeSignals(&oldState);
		return VM_STATUS_SUCCESS;
	}

	/*
		Pops out a new thread and puts it into the queue if possible
	*/
	void scheduleMutex(TVMMutexID mutex){
		threadControlBlock *temp;
		if(!mutexList[mutex]->waitingQ[2].empty()){
			//cerr<<"mutex high"<<endl;
			temp = mutexList[mutex]->waitingQ[2].front();
			mutexList[mutex]->lock = 1;
			mutexList[mutex]->owner = temp->threadID;
			threads[temp->threadID].currState = VM_THREAD_STATE_READY;
			pushQueue(temp->threadID);	// push into the ready q
			mutexList[mutex]->waitingQ[2].pop();
			//high
		}
		else if (!mutexList[mutex]->waitingQ[1].empty()){
			temp = mutexList[mutex]->waitingQ[1].front();
			mutexList[mutex]->lock = 1;
			mutexList[mutex]->owner = temp->threadID;
			threads[temp->threadID].currState = VM_THREAD_STATE_READY;
			pushQueue(temp->threadID);	// push into the ready q
			mutexList[mutex]->waitingQ[1].pop();
			//medium
		}
		else if (!mutexList[mutex]->waitingQ[0].empty()){
			temp = mutexList[mutex]->waitingQ[0].front();
			mutexList[mutex]->lock = 1;
			mutexList[mutex]->owner = temp->threadID;
			threads[temp->threadID].currState = VM_THREAD_STATE_READY;
			pushQueue(temp->threadID);	// push into the ready q
			mutexList[mutex]->waitingQ[0].pop();
			//low
		}
		
	}

	/*
		Frees the mutex and allows other threads to get it. FIFO order
	*/
	TVMStatus VMMutexRelease(TVMMutexID mutex){
		TMachineSignalState oldState;
		MachineSuspendSignals(&oldState);
		if(mutex > mutexList.size()){
			MachineResumeSignals(&oldState);
			return VM_STATUS_ERROR_INVALID_ID;
		}
		if(mutexList[mutex]->lock == 0){
			MachineResumeSignals(&oldState);
			return VM_STATUS_ERROR_INVALID_STATE;
		}
		else{
			mutexList[mutex]->lock = 0;	// unlocking
			/* Check if that current mutex is holding anything or not */
			scheduleMutex(mutex);
		}
		threads[currThread].currState = VM_THREAD_STATE_RUNNING;
		scheduler();
		MachineResumeSignals(&oldState);
		return VM_STATUS_SUCCESS;
	}


	/* 
		Delete a mutex from the VM
	*/
	TVMStatus VMMutexDelete(TVMMutexID mutex){

		if(mutex > mutexList.size()){
			return VM_STATUS_ERROR_INVALID_ID;
		}
		if(mutexList[mutex]->lock == 1 && mutexList[mutex]->owner < 0){
			//it's locked then you can't call it
			return VM_STATUS_ERROR_INVALID_STATE;
		}
		
		TMachineSignalState oldState;
		MachineSuspendSignals(&oldState);
		mutexList.erase(mutexList.begin() + mutex);
		MachineResumeSignals(&oldState);
		return VM_STATUS_SUCCESS;
	}

	// Queries the owner of a mutex in the virtual mchine
	TVMStatus VMMutexQuery(TVMMutexID mutex, TVMThreadIDRef ownerref){

		if(ownerref == NULL){
			return VM_STATUS_ERROR_INVALID_PARAMETER;
		}
		if(mutex > mutexList.size()){
			return VM_STATUS_ERROR_INVALID_ID;
		}
		TMachineSignalState oldState;
		MachineSuspendSignals(&oldState);

		*ownerref = mutexList[mutex]->owner;
		MachineResumeSignals(&oldState);

		return VM_STATUS_SUCCESS;
	}

	TVMStatus VMMemoryPoolCreate(void *base, TVMMemorySize size, TVMMemoryPoolIDRef memory){
		
		//error checking for valid arguements
		if(base == NULL || memory == NULL || size == 0){
			return VM_STATUS_ERROR_INVALID_PARAMETER;
		}
		TMachineSignalState oldState;
		MachineSuspendSignals(&oldState);

		*memory = (unsigned int) memoryPoolList.size();

		memoryPool *temp = new memoryPool(base, size, memory);

		// each time you create a new memory you want to add it to the list.
		memoryPoolList.push_back(temp);
		/*
			Globally there should be a vector of memory pools nad we are just allocating
			space for a new memory pool. The memory reference is just a reference that
			refers to the ID back to the application.
			We are specified the size so we know how much to space to allocate for our
			memory pool.
		*/
		MachineResumeSignals(&oldState);
		return VM_STATUS_SUCCESS;

	}

	TVMStatus VMMemoryPoolAllocate(TVMMemoryPoolID memory, TVMMemorySize size, void **pointer){
		//cerr << "calling allocating memory ~~~ ~" << endl;
		// allocates memory in the memory pool
		if(size == 0 || pointer == NULL || memory > memoryPoolList.size()){
			return VM_STATUS_ERROR_INVALID_PARAMETER;
		}
		TMachineSignalState oldState;
		MachineSuspendSignals(&oldState);
		TVMMemorySize adjustedSize = ((size + MAX_SIZE - 1)/MAX_SIZE) * MAX_SIZE;

		/*
			You want to iterate through all the chunks of free space and find the one that best
			fits into the space. If it fits, then you want to allocate the space, break out of it
			and remove it from the list.
			Then remember to decrement the number of available space.
			if there aren't sufficient amount of space, then you want to return that it didn't have
			enough resources
		*/
		for(list<memoryChunk>::iterator it = memoryPoolList[(int) memory]->freeSpace.begin();
		 		it != memoryPoolList[(int) memory]->freeSpace.end(); it++){
			if(it->length >= adjustedSize){
				// let the application know where the address of the free space is that it just allocated
				*pointer = it->baseAddr;
				//Call the allocate function which make a memory chunk and push it into the allocated list.
				memoryPoolList[memory]->allocate((uint8_t**) pointer, adjustedSize);
				//update the values inside of the freeSpace memory chunk
				it->length -= adjustedSize;
				it->baseAddr += adjustedSize;
				memoryPoolList[memory]->availSPace -= adjustedSize;
				// if it's empty, we want to erase it, no point of keeping a small nothing chunk in free list
				if(it->length == 0){
					memoryPoolList[(int)memory]->freeSpace.erase(it);
				}
				// return and resume
				MachineResumeSignals(&oldState); 
				return VM_STATUS_SUCCESS;
			}
		}

		MachineResumeSignals(&oldState);
		return VM_STATUS_ERROR_INSUFFICIENT_RESOURCES;
	}

	/*
		Checks for whether or not you can free it and then delete it out of the vector
		You can call free or delete
	*/
	TVMStatus VMMemoryPoolDelete(TVMMemoryPoolID memory){
		if(memory > memoryPoolList.size() || memory < 0 ){
			return VM_STATUS_ERROR_INVALID_STATE;
		}
		TMachineSignalState oldState;
		MachineSuspendSignals(&oldState);
		//remove that memorypool from the list
		memoryPoolList.erase(memoryPoolList.begin() + memory);

		MachineResumeSignals(&oldState);
		return VM_STATUS_SUCCESS;
	}

	/*
		Query tells the user how many bytes left there are in that memory pool
	*/
	TVMStatus VMMemoryPoolQuery(TVMMemoryPoolID memory, TVMMemorySizeRef bytesleft){
		
		TMachineSignalState oldState;
		MachineSuspendSignals(&oldState);
		/*
			we keep the number of available space left in our memory pool,
			so we just set bytes left = to that values
		*/
		 
		*bytesleft = memoryPoolList[memory]->availSPace;

		MachineResumeSignals(&oldState);
		return VM_STATUS_SUCCESS;
	}
	
	/*
		The function sort will call this function and it will do the sorting
		 ... we actually don't end up using this..
	*/
	bool sorting(const memoryChunk &uno, const memoryChunk &dos){
		if(uno.baseAddr < dos.baseAddr){
			return true;
		}
		else
			return false;
	}

	/*
		Frees up the space, adds it into the freeList and removes it from
		the allcoated List because we dont need that allocated chunk anymore
	*/
	TVMStatus VMMemoryPoolDeallocate(TVMMemoryPoolID memory, void *pointer){

		if(memory < 0 || memory > memoryPoolList.size() ||
			memoryPoolList[memory]->base == NULL || pointer == NULL){
			return VM_STATUS_ERROR_INVALID_PARAMETER;
		}

		if(memoryPoolList[memory]->allocatedSpace.empty()){
			return VM_STATUS_FAILURE;
		}
		TMachineSignalState oldState;
		MachineSuspendSignals(&oldState);
		/*
			You want to go through all the allocated structs and see if we can find the address
			if we can find the address then we want to see if it can be merged together with 
			surrounding chunks in the free list
		*/	
		for(list<memoryChunk>::iterator it = memoryPoolList[(int) memory]->allocatedSpace.begin();
		 		it != memoryPoolList[(int) memory]->allocatedSpace.end(); it++){
			if(it->baseAddr == pointer){
				// if the free space is empty, then you just want to push it into the list
				if(memoryPoolList[(int)memory]->freeSpace.empty()){
					memoryPoolList[(int)memory]->freeSpace.push_back(*it);
				}
				else{
					// want to go through the list and see where we can actually put the free space in list
					// want to keep it organized and in order so that it's easy to merge
					list<memoryChunk>::iterator freeIt;
					for(freeIt = memoryPoolList[(int) memory]->freeSpace.begin();
						freeIt != memoryPoolList[(int) memory]->freeSpace.end(); freeIt++){
						
						// if we find a space that's less than the baseaddress, then we insert in between
						if(it->baseAddr + it->length < freeIt->baseAddr){
							memoryPoolList[memory]->freeSpace.insert(freeIt, *it);
							break;
						}
						// if the current freeIterators base + len = the current address then we want to left
						if(freeIt->baseAddr + freeIt->length == it->baseAddr){
							freeIt->length += it->length;
							list<memoryChunk>::iterator temp = freeIt;
							temp++;
							// check the right side to see if we can merge with the right, if we can we merge  and erase
							if(freeIt != memoryPoolList[memory]->freeSpace.end() && freeIt->baseAddr + freeIt->length == temp->baseAddr){
								freeIt->length += temp->length;
								memoryPoolList[memory]->freeSpace.erase(temp);
							
							}
							break;
						}
						// if it can only merge with the right, then we just update it
						if(it->baseAddr + it->length == freeIt->baseAddr){
							freeIt->baseAddr = it->baseAddr;
							freeIt->length += it->length;
							//cerr << "Merge to the end" << endl;
							break;
						}	
					}//forloop
					// if it reaches th e end, then we just push it back because there's free space
					if(freeIt == memoryPoolList[memory]->freeSpace.end()){
						memoryPoolList[memory]->freeSpace.push_back(*it);
					}
					
				}
				// update the available space and erase the allocatespace block
				memoryPoolList[memory]->availSPace += it->length;
				memoryPoolList[memory]->allocatedSpace.erase(it);
				MachineResumeSignals(&oldState);
				return VM_STATUS_SUCCESS;
			}
		}

		//otherwise, we return failure

		MachineResumeSignals(&oldState);		
		return VM_STATUS_FAILURE;
	}


}


/*
	NEW STUFF: 
*/
TVMStatus VMDirectoryOpen(const char *dirname, int *dirdescriptor){
	if(dirname == NULL || dirdescriptor == NULL){
		return VM_STATUS_ERROR_INVALID_PARAMETER;
	}

	TMachineSignalState oldState;
	MachineSuspendSignals(&oldState);
	//ensures that it's root..?
	if(strcmp(dirname, "\0") == 0){
		
		openDir temp;
		temp.firstClus = 0;
		//storing the first directory entry in
		temp.directoryEntry = globalDirectory[0].entry;

		//cerr << globalDirectory[0].entry.DShortFileName<< endl;
		temp.directoryID = openDirList.size() + 3;
		temp.next = 0;
		*dirdescriptor = openDirList.size() + 3;
		openDirList.push_back(temp);

		MachineResumeSignals(&oldState);
		return VM_STATUS_SUCCESS;
		
		// return success;
	}
	MachineResumeSignals(&oldState);
	return VM_STATUS_FAILURE;

}

TVMStatus VMDirectoryRead(int dirdescriptor, SVMDirectoryEntryRef dirent){
	if(dirent == NULL){
		return VM_STATUS_ERROR_INVALID_PARAMETER;
	}
	if(dirdescriptor > 0 && dirdescriptor < (int)openDirList.size()){
		return VM_STATUS_FAILURE;
	}
	//cerr << unsigned(globalDirectory[dirdescriptor-3].entry.DShortFileName[0]) << endl;
	if (openDirList[dirdescriptor-3].next >= (int)globalDirectory.size()){
		return VM_STATUS_FAILURE;
	}
	TMachineSignalState oldState;
	MachineSuspendSignals(&oldState);
	//cerr << openDirList[dirdescriptor-3].next << endl;
	//cerr << openDirList[dirdescriptor-3].directoryEntry.DShortFileName << endl;
	*dirent = openDirList[dirdescriptor-3].directoryEntry;
	openDirList[dirdescriptor-3].next++;
	openDirList[dirdescriptor-3].directoryEntry = globalDirectory[openDirList[dirdescriptor-3].next].entry;
	MachineResumeSignals(&oldState);
	return VM_STATUS_SUCCESS;
}


TVMStatus VMDirectoryClose(int dirdescriptor){
	/*
		Modify the modify time and stuff like that.
	*/
	if(dirdescriptor - 3 >= (int) openDirList.size()){
		return VM_STATUS_FAILURE;
	}
	TMachineSignalState oldState;
	MachineSuspendSignals(&oldState);
	//MachineFileClose(dirdescriptor, fileCallback, &threads[currThread]);

	MachineResumeSignals(&oldState);
	return VM_STATUS_SUCCESS;
}

TVMStatus VMDirectoryCurrent(char *abspath){
	if(abspath == NULL){
		return VM_STATUS_ERROR_INVALID_PARAMETER;
	}
	TMachineSignalState oldState;
	MachineSuspendSignals(&oldState);
	strcpy(abspath, currDirectory.c_str());
	MachineResumeSignals(&oldState);
	return VM_STATUS_SUCCESS;
}

TVMStatus VMDirectoryChange(const char *path){
	if(path == NULL){
		return VM_STATUS_ERROR_INVALID_PARAMETER;
	}
	TMachineSignalState oldState;
	MachineSuspendSignals(&oldState);
	if (strcmp(path, ".") == 0 || strcmp(path, "./") == 0|| strcmp(path, "/") == 0){
		// doesn't actually switch to another directory
		MachineResumeSignals(&oldState);
		return VM_STATUS_SUCCESS;
	}
	MachineResumeSignals(&oldState);
	return VM_STATUS_SUCCESS;
}

TVMStatus VMDirectoryRewind(int dirdescriptor){
	TMachineSignalState oldState;
	MachineSuspendSignals(&oldState);
	openDirList[dirdescriptor-3].next = openDirList[dirdescriptor-3].firstClus;
	MachineResumeSignals(&oldState);
	return VM_STATUS_SUCCESS;
}