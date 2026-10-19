/* Authored EEPROM only. Never log serial/MAC/online-key payloads. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <setjmp.h>
static unsigned char memory[65536],before[65536],eeprom[256],settings_cache[256];
static int have_eeprom,settings_cache_valid,expecting_guard;
static SRWLOCK eeprom_lock=SRWLOCK_INIT;
static uint32_t g_eax,g_esp=0x8000;
static jmp_buf guard;
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line%d: %s\n",__LINE__,#x);ExitProcess(1);}}while(0)
static uint32_t *guest_word(uint32_t address)
{
    CHECK(TryAcquireSRWLockExclusive(&eeprom_lock));ReleaseSRWLockExclusive(&eeprom_lock);
    return (uint32_t *)(memory+address);
}
#define MEM32(a) (*guest_word((uint32_t)(a)))
static void stop(const char *reason,uint32_t value)
{
    (void)reason;(void)value;CHECK(expecting_guard);longjmp(guard,1);
}
#include "settings_factory.inc"
static void checksum(void)
{
    /* Independent original add/ADC formulation, not the module helper. */
    uint32_t sum=0,carry=0;memset(eeprom+0x30,0,4);
    for(unsigned i=0x30;i<0x60;i+=4){uint32_t part;memcpy(&part,eeprom+i,4);uint32_t old=sum;sum+=part;carry+=sum<old;}
    uint32_t old=sum;sum+=carry;sum+=sum<old;sum=~sum;memcpy(eeprom+0x30,&sum,4);
}
static void authored(unsigned seed)
{
    for(unsigned i=0;i<256;++i)eeprom[i]=(unsigned char)(i*37+seed);
    checksum();have_eeprom=1;settings_cache_valid=0;
}
static void poison(void){memset(memory,0xcd,sizeof(memory));g_eax=0x12345678;}
static uint32_t query(uint32_t index,uint32_t length,uint32_t result)
{
    uint32_t saved_esp=g_esp;CHECK(settings_factory_query(index,0x1000,0x2000,length,result));
    CHECK(g_esp==saved_esp);return g_eax;
}
static void success(uint32_t index,unsigned size,unsigned type)
{
    PilotFactoryField field;CHECK(pilot_factory_field(index,&field));poison();
    CHECK(!query(index,size+7,0x1004));CHECK(MEM32(0x1000)==type && MEM32(0x1004)==size);
    CHECK(!memcmp(memory+0x2000,settings_cache+field.offset,size));
    for(unsigned i=size;i<size+7;++i)CHECK(!memory[0x2000+i]);
    CHECK(MEM32(0x1ffc)==0xcdcdcdcd && memory[0x2000+size+7]==0xcd);
}
static void guard_query(uint32_t type,uint32_t value,uint32_t length,uint32_t result)
{
    unsigned char cached[256];memcpy(cached,settings_cache,256);
    poison();memcpy(before,memory,sizeof(memory));expecting_guard=1;
    if(!setjmp(guard)){settings_factory_query(0x103,type,value,length,result);CHECK(0);}
    expecting_guard=0;CHECK(!memcmp(before,memory,sizeof(memory)) && !memcmp(cached,settings_cache,256));
    CHECK(g_eax==0x12345678 && g_esp==0x8000);
}
int main(void)
{
    authored(11);CHECK(pilot_factory_checksum(eeprom)==UINT32_MAX);
    success(0x100,12,3);success(0x101,6,3);success(0x102,16,3);success(0x103,4,4);
    CHECK(!memcmp(settings_cache,eeprom,256));
    poison();CHECK(!query(0x103,4,0) && MEM32(0x1004)==0xcdcdcdcd); /* actual caller */
    for(unsigned size=0;size<4;++size){poison();memcpy(before,memory,sizeof(memory));
        CHECK(query(0x103,size,0x1004)==0xc0000023u && !memcmp(before,memory,sizeof(memory)));}
    /* Shared first-read cache survives raw input changes and an already-owned
     * user snapshot is reused, rather than creating another factory epoch. */
    unsigned char old[256];memcpy(old,settings_cache,256);eeprom[0x58]^=0x80;checksum();
    success(0x103,4,4);CHECK(!memcmp(old,settings_cache,256));
    authored(19);memcpy(settings_cache,old,256);settings_cache_valid=1;
    success(0x103,4,4);CHECK(!memcmp(old,settings_cache,256));
    /* Store order: type then result, followed by zero/copy of value. */
    poison();CHECK(settings_factory_query(0x100,0x1000,0x2000,12,0x1000) && !g_eax && MEM32(0x1000)==12);
    poison();CHECK(settings_factory_query(0x100,0x2000,0x2000,16,0x1004) && !g_eax);
    CHECK(!memcmp(memory+0x2000,old+0x34,12) && !MEM32(0x200c));
    poison();CHECK(settings_factory_query(0x100,0x1000,0x2000,16,0x2000) && !g_eax);
    CHECK(!memcmp(memory+0x2000,old+0x34,12) && !MEM32(0x200c));
    guard_query(0,0x2000,4,0);guard_query(0xfffffffdu,0x2000,4,0);
    guard_query(0x1000,0,4,0);guard_query(0x1000,0xfffffffdu,4,0);
    guard_query(0x1000,0x2000,4,0xfffffffdu);
    have_eeprom=0;guard_query(0x1000,0x2000,4,0);have_eeprom=1;
    const uint32_t unsupported[]={0,8,0xff,0x104,0x105,0x1ff,0xfffe,0xffff};
    for(unsigned i=0;i<sizeof(unsupported)/sizeof(unsupported[0]);++i){poison();memcpy(before,memory,sizeof(memory));
        CHECK(!settings_factory_query(unsupported[i],0,0,0,0) && g_eax==0x12345678 && !memcmp(before,memory,sizeof(memory)));}
    authored(31);eeprom[0x34]^=1;
    for(unsigned size=0;size<=4;++size){poison();memcpy(before,memory,sizeof(memory));
        CHECK(query(0x103,size,0x1004)==0xc000009cu && !memcmp(before,memory,sizeof(memory)));}
    CHECK(settings_cache_valid); /* Failed checksum keeps the shared snapshot. */
    checksum();poison();CHECK(query(0x103,4,0)==0xc000009cu);
    /* Invalid output pointers are unused on checksum/size failure. */
    CHECK(settings_factory_query(0x103,0,0,0,0xfffffffdu) && g_eax==0xc000009cu);
    authored(43);poison();CHECK(settings_factory_query(0x103,0,0,0,0xfffffffdu) && g_eax==0xc0000023u);
    memset(eeprom,0xff,256);settings_cache_valid=0;poison();memcpy(before,memory,sizeof(memory));
    CHECK(query(0x103,4,0x1004)==0xc0000184u && !settings_cache_valid && !memcmp(before,memory,sizeof(memory)));
    authored(47);success(0x103,4,4); /* Failed all-FF initialization can retry. */
    CHECK(TryAcquireSRWLockExclusive(&eeprom_lock));ReleaseSRWLockExclusive(&eeprom_lock);
    puts("PASS: factory100..103 real-shaped field ABI/checksum/cache, failure precedence, output aliases, range guards and read-only ownership");
    return 0;
}
