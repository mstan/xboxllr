/* Authored Xbox counted-string construction. Original289/290 observations
 * establish wrapping16-bit byte lengths, unlike desktop NT's saturation.
 * Buffer retains the caller's exact pointer; no allocation or copy occurs. */
static void counted_string_init(unsigned unit,uint32_t output,uint32_t source)
{
    size_t bytes=0;
    if(source) {
        if(unit==1)bytes=strlen((const char*)(memory+source));
        else while(MEM16((uint32_t)(source+bytes)))bytes+=2;
    }
    MEM16(output)=(uint16_t)bytes;
    MEM16(output+2)=source?(uint16_t)(bytes+unit):0;
    MEM32(output+4)=source;
}

/* Executing original316 for all256 byte values establishes these ranges and
 * FF's unrepresentable uppercase result. No private character table is copied. */
static uint8_t counted_string_upper(uint8_t value)
{
    if(value==0xff)return '?';
    if((value>='a' && value<='z') || (value>=0xe0 && value<=0xf6) ||
       (value>=0xf8 && value<=0xfe))return (uint8_t)(value-32);
    return value;
}
static uint32_t counted_string_equal(uint32_t left,uint32_t right,uint8_t insensitive)
{
    uint32_t bytes=MEM16(left);
    if(bytes!=MEM16(right))return 0;
    if(!bytes)return 1;
    const uint8_t *a=memory+MEM32(left+4),*b=memory+MEM32(right+4);
    if(!insensitive)return memcmp(a,b,bytes)==0;
    for(uint32_t i=0;i<bytes;++i)
        if(counted_string_upper(a[i])!=counted_string_upper(b[i]))return 0;
    return 1;
}
