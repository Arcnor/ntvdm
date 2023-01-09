// 8086 emulator
// Written by David Lee
// Useful: http://bitsavers.org/components/intel/8086/9800722-03_The_8086_Family_Users_Manual_Oct79.pdf
//         https://www.eeeguide.com/8086-instruction-format/
//         https://www.felixcloutier.com/x86
//         https://onlinedisassembler.com/odaweb/

#include <stdio.h>
#include <memory.h>
#include <assert.h>
#include <vector>
#include <djltrace.hxx>
#include <djl8086d.hxx>

using namespace std;

#include "i8086.hxx"

uint8_t memory[ 1024 * 1024 ];
i8086 cpu;
static CDisassemble8086 dis;
static uint32_t g_State = 0;

const DWORD stateTraceInstructions = 1;
const DWORD stateEndEmulation = 2;

void i8086::trace_instructions( bool t ) { if ( t ) g_State |= stateTraceInstructions; else g_State &= ~stateTraceInstructions; }
void i8086::end_emulation() { g_State |= stateEndEmulation; }

extern void DumpBinaryData( byte * pData, DWORD length, DWORD indent );
void i8086::trace_state()
{
    uint8_t * pcode = memptr( flat_ip() );
    const char * pdisassemble = dis.Disassemble( pcode );
    tracer.TraceQuiet( "ip %#6x, opcode %02x %02x %02x %02x %02x, ax %04x, bx %04x, cx %04x, dx %04x, di %04x, si %04x, ds %04x, es %04x, cs %04x, ss %04x, bp %04x, sp %04x, %s  %s ; (%u)\n",
                       ip, *pcode, (byte) pcode[1], (byte) pcode[2], (byte) pcode[3], (byte) pcode[4],
                       ax, bx, cx, dx, di, si, ds, es, cs, ss, bp, sp, render_flags(), pdisassemble, dis.BytesConsumed() );
//    DumpBinaryData( memory + flatten( ss, 0x11ac ), 4, 0 );
//    DumpBinaryData( memory + flatten( ss, 0xffa0 ), 3 * 32, 0 );
} //trace_state

void i8086::update_rep_sidi16()
{
    if ( fDirection )
    {
        si -= 2;
        di -= 2;
    }
    else
    {
        si += 2;
        di += 2;
    }
} //update_rep_sidi16

void i8086::update_rep_sidi8()
{
    if ( fDirection )
    {
        si--;
        di--;
    }
    else
    {
        si++;
        di++;
    }
} //update_rep_sidi8

uint8_t i8086::op_sub8( uint8_t lhs, uint8_t rhs, bool borrow )
{
    // com == ones-complement

    uint8_t com_rhs = ~rhs;
    uint8_t borrow_int = borrow ? 0 : 1;
    uint16_t res16 =  (uint16_t) lhs + (uint16_t) com_rhs + (uint16_t) borrow_int;
    uint8_t res8 = res16 & 0xff;

    fCarry = ( 0 == ( res16 & 0x100 ) );
    set_PZS8( res8 );

    // if not ( ( one of lhs and com_x are negative ) and ( one of lhs and result are negative ) )

    fOverflow = ! ( ( lhs ^ com_rhs ) & 0x80 ) && ( ( lhs ^ res8 ) & 0x80 );
    fAuxCarry = ( 0 != ( ( ( lhs & 0xf ) + ( com_rhs & 0xf ) + borrow_int ) & 0x10 ) );
    return res8;
} //op_sub8

uint16_t i8086::op_sub16( uint16_t lhs, uint16_t rhs, bool borrow )
{
    // com == ones-complement

    uint16_t com_rhs = ~rhs;
    uint16_t borrow_int = borrow ? 0 : 1;
    uint32_t res32 =  (uint32_t) lhs + (uint32_t) com_rhs + (uint32_t) borrow_int;
    uint16_t res16 = res32 & 0xffff;
    fCarry = ( 0 == ( res32 & 0x10000 ) );
    set_PZS16( res16 );
    fOverflow = ( ! ( ( lhs ^ com_rhs ) & 0x8000 ) ) && ( ( lhs ^ res16 ) & 0x8000 );
    fAuxCarry = ( 0 != ( ( ( lhs & 0xfff ) + ( com_rhs & 0xfff ) + borrow_int ) & 0x1000 ) );
    return res16;
} //op_sub16

uint16_t i8086::op_add16( uint16_t lhs, uint16_t rhs, bool carry )
{
    uint32_t carry_int = carry ? 1 : 0;
    uint32_t r32 = (uint32_t) lhs + (uint32_t) rhs + carry_int;
    uint16_t r16 = r32 & 0xffff;
    fCarry = ( 0 != ( r32 & 0x010000 ) );
    fAuxCarry = ( 0 != ( ( ( 0xfff & lhs ) + ( 0xfff & rhs ) + carry_int ) & 0x1000 ) );
    set_PZS16( r16 );
    fOverflow = ( ! ( ( lhs ^ rhs ) & 0x8000 ) ) && ( ( lhs ^ r16 ) & 0x8000 );
    return r16;
} //op_add16

uint8_t i8086::op_add8( uint8_t lhs, uint8_t rhs, bool carry )
{
    uint16_t carry_int = carry ? 1 : 0;
    uint16_t r16 = (uint16_t) lhs + (uint16_t) rhs + carry_int;
    uint8_t r8 = r16 & 0xff;
    fCarry = ( 0 != ( r16 & 0x0100 ) );
    fAuxCarry = ( 0 != ( ( ( 0xf & lhs ) + ( 0xf & rhs ) + carry_int ) & 0x10 ) );
    set_PZS8( r8 );
    fOverflow = ( ! ( ( lhs ^ rhs ) & 0x80 ) ) && ( ( lhs ^ r8 ) & 0x80 );
    return r8;
} //op_add8

uint16_t i8086::op_and16( uint16_t lhs, uint16_t rhs )
{
    lhs &= rhs;
    set_PZS16( lhs );
    reset_carry_overflow();
    return lhs;
} //op_and16

uint8_t i8086::op_and8( uint8_t lhs, uint8_t rhs )
{
    lhs &= rhs;
    set_PZS8( lhs );
    reset_carry_overflow();
    return lhs;
} //op_and8

uint16_t i8086::op_or16( uint16_t lhs, uint16_t rhs )
{
    lhs |= rhs;
    set_PZS16( lhs );
    reset_carry_overflow();
    return lhs;
} //op_or16

uint16_t i8086::op_xor16( uint16_t lhs, uint16_t rhs )
{
    lhs ^= rhs;
    set_PZS16( lhs );
    reset_carry_overflow();
    return lhs;
} //op_xor16

uint8_t i8086::op_or8( uint8_t lhs, uint8_t rhs )
{
    lhs |= rhs;
    set_PZS8( lhs );
    reset_carry_overflow();
    return lhs;
} //op_or8

uint8_t i8086::op_xor8( uint8_t lhs, uint8_t rhs )
{
    lhs ^= rhs;
    set_PZS8( lhs );
    reset_carry_overflow();
    return lhs;
} //op_xor8

void i8086::do_math8( uint8_t math, uint8_t * psrc, uint8_t rhs )
{
    assert( math <= 7 );
    if ( 0 == math ) *psrc = op_add8( *psrc, rhs );
    else if ( 1 == math ) *psrc = op_or8( *psrc, rhs );
    else if ( 2 == math ) *psrc = op_add8( *psrc, rhs, fCarry );
    else if ( 3 == math ) *psrc = op_sub8( *psrc, rhs, fCarry );
    else if ( 4 == math ) *psrc = op_and8( *psrc, rhs );
    else if ( 5 == math ) *psrc = op_sub8( *psrc, rhs );
    else if ( 6 == math ) *psrc = op_xor8( *psrc, rhs );
    else op_sub8( *psrc, rhs ); // 7 == math
} //do_math8

void i8086::do_math16( uint8_t math, uint16_t * psrc, uint16_t rhs )
{
    assert( math <= 7 );
    if ( 0 == math ) *psrc = op_add16( *psrc, rhs );
    else if ( 1 == math ) *psrc = op_or16( *psrc, rhs );
    else if ( 2 == math ) *psrc = op_add16( *psrc, rhs, fCarry );
    else if ( 3 == math ) *psrc = op_sub16( *psrc, rhs, fCarry );
    else if ( 4 == math ) *psrc = op_and16( *psrc, rhs );
    else if ( 5 == math ) *psrc = op_sub16( *psrc, rhs );
    else if ( 6 == math ) op_xor16( *psrc, rhs );
    else op_sub16( *psrc, rhs ); // 7 == math
} //do_math16

uint8_t i8086::op_inc8( uint8_t val )
{
   fOverflow = ( 0x7f == val );
   val++;
   fAuxCarry = ( 0 == ( val & 0xf ) );
   set_PZS8( val );
   return val;
} //op_inc8

uint8_t i8086::op_dec8( uint8_t val )
{
   val--;
   fOverflow = ( 0x80 == val );
   fAuxCarry = ( 0xf == ( val & 0xf ) );
   set_PZS8( val );
   return val;
} //op_dec8

uint16_t i8086::op_inc16( uint16_t val )
{
   fOverflow = ( 0x7fff == val );
   val++;
   fAuxCarry = ( 0 == ( val & 0xfff ) );
   set_PZS16( val );
   return val;
} //op_inc16

uint16_t i8086::op_dec16( uint16_t val )
{
   val--;
   fOverflow = ( 0x8000 == val );
   fAuxCarry = ( 0xfff == ( val & 0xfff ) );
   set_PZS16( val );
   return val;
} //op_dec16

void i8086::op_rol16( uint16_t * pval, uint8_t shift )
{
    if ( 0 == shift )
        return;

    uint16_t val = *pval;
    for ( uint8_t sh = 0; sh < shift; sh++ )
    {
        bool highBit = ( 0 != ( 0x8000 & val ) );
        val <<= 1;
        if ( highBit )
            val |= 1;
        else
            val &= 0xfffe;
        fCarry = highBit;
    }

    if ( 1 == shift )
        fOverflow = ( ( 0 != ( val & 0x8000 ) ) ^ fCarry );

    *pval = val;
} //rol16

void i8086::op_ror16( uint16_t * pval, uint8_t shift )
{
    if ( 0 == shift )
        return;

    uint16_t val = *pval;
    for ( uint8_t sh = 0; sh < shift; sh++ )
    {
        bool lowBit = ( 0 != ( 1 & val ) );
        val >>= 1;
        if ( lowBit )
            val |= 0x8000;
        else
            val &= 0x7fff;
        fCarry = lowBit;
    }

    // Overflow only defined for 1-bit shifts
    if ( 1 == shift )
        fOverflow = ( ( 0 != ( val & 0x8000 ) ) ^ ( 0 != ( val & 0x4000 ) ) );
    else
        fOverflow = true;

    *pval = val;
} //ror16

void i8086::op_rcl16( uint16_t * pval, uint8_t shift )
{
    if ( 0 == shift )
        return;

    uint16_t val = *pval;
    for ( uint8_t sh = 0; sh < shift; sh++ )
    {
        bool newCarry = ( 0 != ( 0x8000 & val ) );
        val <<= 1;
        if ( fCarry )
            val |= 1;
        else
            val &= 0xfffe;
        fCarry = newCarry;
    }

    if ( 1 == shift )
        fOverflow = ( ( 0 != ( val & 0x8000 ) ) ^ fCarry );

    *pval = val;
} //rcl16

void i8086::op_rcr16( uint16_t * pval, uint8_t shift )
{
    if ( 0 == shift )
        return;

    uint16_t val = *pval;
    for ( uint8_t sh = 0; sh < shift; sh++ )
    {
        bool newCarry = ( 0 != ( 1 & val ) );
        val >>= 1;
        if ( fCarry )
            val |= 0x8000;
        else
            val &= 0x7fff;
        fCarry = newCarry;
    }

    if ( 1 == shift )
        fOverflow = ( ( 0 != ( val & 0x8000 ) ) ^ ( 0 != ( val & 0x4000 ) ) );

    *pval = val;
} //rcr16

void i8086::op_sal16( uint16_t * pval, uint8_t shift )
{
    *pval <<= ( shift - 1 );
    fCarry = ( 0 != ( *pval & 0x8000 ) );
    *pval <<= 1;

    if ( 1 == shift )
        fOverflow = ! ( ( 0 != ( *pval & 0x8000 ) ) == fCarry );

    set_PZS16( *pval );
} //sal16

void i8086::op_shr16( uint16_t * pval, uint8_t shift )
{
    fOverflow = ( 0 != ( *pval & 0x8000 ) );
    *pval >>= ( shift - 1 );
    fCarry = ( 0 != ( *pval & 1 ) );
    *pval >>= 1;
    set_PZS16( *pval );
} //shr16

void i8086::op_sar16( uint16_t * pval, uint8_t shift )
{
    if ( 0 == shift )
        return;

    uint16_t val = *pval;
    bool highBit = ( 0 != ( val & 0x8000 ) );
    for ( uint8_t sh = 0; sh < shift; sh++ )
    {
        fCarry = ( 0 != ( 1 & val ) );
        val >>= 1;
        if ( highBit )
            val |= 0x8000;
        else
            val &= 0x7fff;
    }

    if ( 1 == shift )
        fOverflow = false;

    set_PZS16( val );
    *pval = val;
} //sar16

void i8086::op_rol8( uint8_t * pval, uint8_t shift )
{
    uint8_t val = *pval;
    for ( uint8_t sh = 0; sh < shift; sh++ )
    {
        bool highBit = ( 0 != ( 0x80 & val ) );
        val <<= 1;
        if ( highBit )
            val |= 1;
        else
            val &= 0xfe;
        fCarry = highBit;
    }

    if ( 1 == shift )
        fOverflow = ( ( 0 != ( val & 0x80 ) ) ^ fCarry );

    *pval = val;
} //rol8

void i8086::op_ror8( uint8_t * pval, uint8_t shift )
{
    if ( 0 == shift )
        return;

    uint8_t val = *pval;
    for ( uint8_t sh = 0; sh < shift; sh++ )
    {
        bool lowBit = ( 0 != ( 1 & val ) );
        val >>= 1;
        if ( lowBit )
            val |= 0x80;
        else
            val &= 0x7f;
        fCarry = lowBit;
    }

    if ( 1 == shift )
        fOverflow = ( ( 0 != ( val & 0x80 ) ) ^ ( 0 != ( val & 0x40 ) ) );

    *pval = val;
} //ror8

void i8086::op_rcl8( uint8_t * pval, uint8_t shift )
{
    if ( 0 == shift )
        return;

    uint8_t val = *pval;
    for ( uint8_t sh = 0; sh < shift; sh++ )
    {
        bool newCarry = ( 0 != ( 0x80 & val ) );
        val <<= 1;
        if ( fCarry )
            val |= 1;
        else
            val &= 0xfe;
        fCarry = newCarry;
    }

    if ( 1 == shift )
        fOverflow = ( ( 0 != ( val & 0x80 ) ) ^ fCarry );

    *pval = val;
} //rcl8

void i8086::op_rcr8( uint8_t * pval, uint8_t shift )
{
    if ( 0 == shift )
        return;

    uint8_t val = *pval;
    for ( uint8_t sh = 0; sh < shift; sh++ )
    {
        bool newCarry = ( 0 != ( 1 & val ) );
        val >>= 1;
        if ( fCarry )
            val |= 0x80;
        else
            val &= 0x7f;
        fCarry = newCarry;
    }

    if ( 1 == shift )
        fOverflow = ( ( 0 != ( val & 0x80 ) ) ^ ( 0 != ( val & 0x40 ) ) );

    *pval = val;
} //rcr8

void i8086::op_sal8( uint8_t * pval, uint8_t shift )
{
    *pval <<= ( shift - 1 );
    fCarry = ( 0 != ( *pval & 0x80 ) );
    *pval <<= 1;

    if ( 1 == shift )
        fOverflow = ! ( ( 0 != ( *pval & 0x8000 ) ) == fCarry );

    set_PZS8( *pval );
} //sal8

void i8086::op_shr8( uint8_t * pval, uint8_t shift )
{
    fOverflow = ( 0 != ( *pval & 0x80 ) );
    *pval >>= ( shift - 1 );
    fCarry = ( 0 != ( *pval & 1 ) );
    *pval >>= 1;
    set_PZS8( *pval );
} //shr8

void i8086::op_sar8( uint8_t * pval, uint8_t shift )
{
    if ( 0 == shift )
        return;

    uint8_t val = *pval;
    bool highBit = ( 0 != ( val & 0x80 ) );
    for ( uint8_t sh = 0; sh < shift; sh++ )
    {
        fCarry = ( 0 != ( 1 & val ) );
        val >>= 1;
        if ( highBit )
            val |= 0x80;
        else
            val &= 0x7f;
    }

    if ( 1 == shift )
        fOverflow = false;

    set_PZS16( val );
    *pval = val;
} //sar8

void i8086::op_cmps16()
{
    op_sub16( * flat_address16( get_seg_value( ds ), si ), * flat_address16( es, di ) );
    update_rep_sidi16();
} //op_cmps16

void i8086::op_cmps8()
{
    op_sub8( * flat_address8( get_seg_value( ds ), si ), * flat_address8( es, di ) );
    update_rep_sidi8();
} //op_cmps8

void i8086::op_movs16()
{
    * flat_address16( es, di ) = * flat_address16( get_seg_value( ds ), si );
    update_rep_sidi16();
} //op_movs16

void i8086::op_movs8()
{
    * flat_address8( es, di ) = * flat_address8( get_seg_value( ds ), si );
    update_rep_sidi8();
} //op_movs8

void i8086::op_sto16()
{
    * flat_address16( es, di ) = ax;
    if ( fDirection )
        di -= 2;
    else
        di += 2;
} //op_sto16

void i8086::op_sto8()
{
    * flat_address8( es, di ) = al();
    if ( fDirection )
        di--;
    else
        di++;
} //op_sto8

void i8086::op_lods16()
{
    ax = * flat_address16( get_seg_value( ds ), si );
    if ( fDirection )
        si -= 2;
    else
        si += 2;
} //op_lods16

void i8086::op_lods8()
{
    set_al( * flat_address8( get_seg_value( ds ), si ) );
    if ( fDirection )
        si--;
    else
        si++;
} //op_lods8

void i8086::op_scas16()
{
    op_sub16( ax, * flat_address16( get_seg_value( es ), di ) );
    if ( fDirection )
        di -= 2;
    else
        di += 2;
} //op_scas16

void i8086::op_scas8()
{
    op_sub8( al(), * flat_address8( get_seg_value( es ), di ) );
    if ( fDirection )
        di--;
    else
        di++;
} //op_scas8

uint64_t i8086::emulate( uint64_t maxcycles )
{
    uint64_t cycles = 0;

    while ( cycles < maxcycles )                   // 4% of runtime
    {
        prefix_segment_override = 0xff;            // 1% of runtime (the compiler does both at once!)
        prefix_repeat_opcode = 0xff;

_after_prefix:
        cycles++;                                  // 7% of runtime. for now, it's just instructions not cycles. to be added...

        if ( 0 != g_State )                        //.5% of runtime. grouped into one check rather than 2 every loop
        {
            if ( g_State & stateEndEmulation )
            {
                g_State &= ~stateEndEmulation;
                break;
            }

            if ( g_State & stateTraceInstructions )
                trace_state();
        }

        decode_instruction( memptr( flat_ip() ) ); // 32 % of runtime

        bool handled = true;                       
        switch( _b0 )                              // 20% of runtime setting up for the jump table
        {
            case 0x04: { set_al( op_add8( al(), _b1 ) ); _bc++; break; } // add al, immed8
            case 0x05: { ax = op_add16( ax, _b12 ); _bc += 2; break; } // add ax, immed16
            case 0x06: { push( es ); break; } // push es
            case 0x07: { es = pop(); break; } // pop es
            case 0x0c: { _bc++; set_al( op_or8( al(), _b1 ) ); break; } // or al, immed8
            case 0x0d: { _bc += 2; ax = op_or16( ax, _b12 ); break; } // or ax, immed16
            case 0x0e: { push( cs ); break; } // push cs
            case 0x14: { _bc++; set_al( op_add8( al(), _b1, fCarry ) ); break; } // adc al, immed8
            case 0x15: { _bc += 2; ax = op_add16( ax, _b12, fCarry ); break; } // adc ax, immed16
            case 0x16: { push( ss ); break; } // push ss
            case 0x17: { ss = pop(); break; } // pop ss
            case 0x1c: { _bc++; set_al( op_sub8( al(), _b1, fCarry ) ); break; } // sbb al, immed8
            case 0x1d: { _bc += 2; ax = op_sub16( ax, _b12, fCarry ); break; } // sbb ax, immed16
            case 0x1e: { push( ds ); break; } // push ds
            case 0x1f: { ds = pop(); break; } // pop ds
            case 0x24: { _bc++; set_al( op_and8( al(), _b1 ) ); break; } // and al, immed8
            case 0x25: { _bc += 2; ax = op_and16( ax, _b12 ); break; } // and ax, immed16
            case 0x26: { prefix_segment_override = 0; ip++; goto _after_prefix; } // es segment override
            case 0x27: // daa
            {
                uint8_t loNibble = al() & 0xf;
                uint8_t toadd = 0;
                if ( fAuxCarry || ( loNibble > 9 ) )
                    toadd = 6;
    
                bool carry = fCarry;
                uint8_t hiNibble = al() & 0xf0;
                if ( ( hiNibble > 0x90 ) || ( hiNibble >= 0x90 && loNibble > 0x9 ) || carry )
                {
                    toadd |= 0x60;
                    carry = true;
                }
    
                set_al( op_add8( al(), toadd ) );
                fCarry = carry; // this doesn't change regardless of the result
                break;
            }
            case 0x2c: { _bc++; set_al( op_sub8( al(), _b1 ) ); break; } // sub al, immed8
            case 0x2d: { _bc += 2; ax = op_sub16( ax, _b12 ); break; } // sub ax, immed16
            case 0x2e: { prefix_segment_override = 1; ip++; goto _after_prefix; } // cs segment override
            case 0x34: { _bc++; set_al( op_xor8( al(), _b1 ) ); break; } // xor al, immed8
            case 0x35: { _bc += 2; ax = op_xor16( ax, _b12 ); break; } // xor ax, immed16
            case 0x36: { prefix_segment_override = 2; ip++; goto _after_prefix; } // ss segment override
            case 0x3c: { _bc++; op_sub8( al(), _b1 ); break; } // cmp al, i8
            case 0x3d: { _bc += 2; op_sub16( ax, _b12 ); break; } // cmp ax, i16
            case 0x3e: { prefix_segment_override = 3; ip++; goto _after_prefix; } // ds segment override
            case 0x69: // FAKE Opcode: i8086_opcode_interrupt
            {
                i8086_invoke_interrupt( last_interrupt );
                break;
            }
            case 0x84: // test reg8/mem8, reg8
            {
                _bc++;
                uint16_t src;
                uint8_t * pleft = (uint8_t *) get_op_args( toreg(), src );
                op_and8( *pleft, src & 0xff );
                break;
            }
            case 0x85: // test reg16/mem16, reg16
            {
                _bc++;
                uint16_t src;
                uint16_t * pleft = (uint16_t *) get_op_args( toreg(), src );
                op_and16( *pleft, src );
                break;
            }
            case 0x86: // xchg reg8, reg8/mem8
            {
                uint8_t * pA = get_preg8( _reg );
                uint8_t * pB = (uint8_t *) get_rm_ptr( _rm );
                uint8_t tmp = *pB;
                *pB = *pA;
                *pA = tmp;
                _bc++;
                break;
            }
            case 0x87: // xchg reg16, reg16/mem16
            {
                uint16_t * pA = get_preg16( _reg );
                uint16_t * pB = (uint16_t *) get_rm_ptr( _rm );
                uint16_t tmp = *pB;
                *pB = *pA;
                *pA = tmp;
                _bc++;
                break;
            }
            case 0x88: // mov reg8/mem8, reg8
            {
                _bc++;
                uint16_t src;
                void * pdst = get_op_args( toreg(), src );
                * (uint8_t *) pdst = src & 0xff;
                break;
            }
            case 0x89: // mov reg16/mem16, reg16
            {
                _bc++;
                uint16_t src;
                void * pdst = get_op_args( toreg(), src );
                * (uint16_t *) pdst = src;
                break;
            }
            case 0x8a: { _bc++; * get_preg8( _reg ) = * (uint8_t *) get_rm_ptr( _rm ); break; } // mov reg8, r/m8
            case 0x8b: { _bc++; * get_preg16( _reg ) = * (uint16_t *) get_rm_ptr( _rm ); break; } // mov reg16, r/m16
            case 0x8c: { _bc++; * get_rm16_ptr() = * seg_reg( _reg ); break; } // mov r/m16, sreg
            case 0x8d: { _bc++; * get_preg16( _reg ) = get_rm_ea( _rm ); break; } // lea reg16, mem16
            case 0x8e: // mov sreg, reg16/mem16
            {
                 _isword = true; // the opcode indicates it's a byte instruction, but it's not
                 * seg_reg( _reg ) = * (uint16_t *) get_rm_ptr( _rm );
                 _bc++;
                 break;
            }
            case 0x8f: // pop reg16/mem16
            {
                uint16_t * pdst = (uint16_t * ) get_rm_ptr( _rm );
                *pdst = pop();
                _bc++;
                break;
            }
            case 0x90: { break; } // nop
            case 0x98: { set_ah( ( al() & 0x80 ) ? 0xff : 0 ); break; } // cbw -- covert byte in al to word in ax. sign extend
            case 0x99: { dx = ( ax & 0x8000 ) ? 0xffff : 0; break; } // cwd -- convert word in ax to to double-word in dx:ax. sign extend
            case 0x9a: // call far proc
            {
                push( cs );
                push( ip + 5 );
                ip = _b12;
                cs = (uint16_t) _pcode[3] | ( (uint16_t) _pcode[ 4 ]  << 8 );
                continue;
            }
            case 0x9b: break; // wait for pending floating point exceptions
            case 0x9c: // pushf
            {
                materializeFlags();
                push( flags );
                break;
            }
            case 0x9d: // popf
            {
                flags = pop();
                unmaterializeFlags();
                break;
            }
            case 0x9e: // sahf -- stores a subset of flags from ah
            {
                uint8_t fl = ah();
                fSign = ( 0 != ( fl & 0x80 ) );
                fZero = ( 0 != ( fl & 0x40 ) );
                fAuxCarry = ( 0 != ( fl & 0x20 ) );
                fParityEven = ( 0 != ( fl & 0x04 ) );
                fCarry = ( 0 != ( fl & 1 ) );
                break;
            }
            case 0x9f: // lahf -- loads a subset of flags to ah
            {
                uint8_t fl = 0x02;
                if ( fSign ) fl |= 0x80;
                if ( fZero ) fl |= 0x40;
                if ( fAuxCarry ) fl |= 0x10;
                if ( fParityEven ) fl |= 0x04;
                if ( fCarry ) fl |= 1;
                set_ah( fl );
                break;
            }
            case 0xa0: // mov al, mem8
            {
                uint32_t flat = flatten( get_seg_value( ds ), _b12 );
                set_al( * (uint8_t *) ( memory + flat ) );
                _bc += 2;
                break;
            }
            case 0xa1: // mov ax, mem16
            {
                uint32_t flat = flatten( get_seg_value( ds ), _b12 );
                ax = * (uint16_t *) ( memory + flat );
                _bc += 2;
                break;
            }
            case 0xa2: // mov mem8, al
            {
                uint8_t * pdst = (uint8_t *) ( memory + flatten( get_seg_value( ds ), _b12 ) );
                *pdst = al();
                _bc += 2;
                break;
            }
            case 0xa3: // mov mem16, ax
            {
                uint16_t * pdst = (uint16_t *) ( memory + flatten( get_seg_value( ds ), _b12 ) );
                *pdst = ax;
                _bc += 2;
                break;
            }
            case 0xa4: // movs dst-str8, src-str8
            {
                if ( 0xf3 == prefix_repeat_opcode || 0xf2 == prefix_repeat_opcode ) // f2 here in ms-dos link.exe v2.0
                {
                    while ( 0 != cx )
                    {
                        op_movs8();
                        cx--;
                    }
                }
                else
                    op_movs8();
                break;
            }
            case 0xa5: // movs dest-str16, src-str16
            {
                if ( 0xf3 == prefix_repeat_opcode || 0xf2 == prefix_repeat_opcode ) // f2 here in ms-dos link.exe v2.0
                {
                    while ( 0 != cx )
                    {
                        op_movs16();
                        cx--;
                    }
                }
                else
                    op_movs16();
                break;
            }
            case 0xa6: // cmps m8, m8
            {
                if ( 0xf2 == prefix_repeat_opcode )
                {
                    while ( 0 != cx )
                    {
                        op_cmps8();
                        cx--;
                        if ( fZero )
                            break;
                    }
                }
                else if ( 0xf3 == prefix_repeat_opcode )
                {
                    while ( 0 != cx )
                    {
                        op_cmps8();
                        cx--;
                        if ( !fZero )
                            break;
                    }
                }
                else
                    op_cmps8();
                break;
            }
            case 0xa7: // cmps dest-str15, src-str16
            {
                if ( 0xf2 == prefix_repeat_opcode )
                {
                    while ( 0 != cx )
                    {
                        op_cmps16();
                        cx--;
                        if ( fZero )
                            break;
                    }
                }
                else if ( 0xf3 == prefix_repeat_opcode )
                {
                    while ( 0 != cx )
                    {
                        op_cmps16();
                        cx--;
                        if ( !fZero )
                            break;
                    }
                }
                else
                    op_cmps16();
                break;
            }
            case 0xa8: { _bc++; op_and8( al(), _b1 ); break; } // test al, immed8
            case 0xa9: // test ax, immed16
            {
                _bc += 2;
                op_and16( ax, _b12 );
                break;
            }
            case 0xaa: // stos8 -- fill bytes with al
            {
                if ( 0xf3 == prefix_repeat_opcode || 0xf2 == prefix_repeat_opcode )
                {
                    while ( 0 != cx )
                    {
                        op_sto8();
                        cx--;
                    }
                }
                else
                    op_sto8();
                break;
            }
            case 0xab: // stos16 -- fill words with ax
            {
                if ( 0xf3 == prefix_repeat_opcode || 0xf2 == prefix_repeat_opcode ) // f2 here in ms-dos link.exe v2.0
                {
                    while ( 0 != cx )
                    {
                        op_sto16();
                        cx--;
                    }
                }
                else
                    op_sto16();
                break;
            }
            case 0xac: // lods8 src-str8
            {
                if ( 0xf3 == prefix_repeat_opcode || 0xf2 == prefix_repeat_opcode ) // f2 here is illegal but used
                {
                    while ( 0 != cx )
                    {
                        op_lods8();
                        cx--;
                    }
                }
                else
                    op_lods8();
                break;
            }
            case 0xad: // lods16 src-str16
            {
                if ( 0xf3 == prefix_repeat_opcode || 0xf2 == prefix_repeat_opcode ) // f2 here is illegal but used
                {
                    while ( 0 != cx )
                    {
                        op_lods16();
                        cx--;
                    }
                }
                else
                    op_lods16();
                break;
            }
            case 0xae: // scas8 compare al with byte at es:di
            {
                if ( 0xf2 == prefix_repeat_opcode )
                {
                    while ( 0 != cx )
                    {
                        op_scas8();
                        cx--;
                        if ( fZero )
                            break;
                    }
                }
                else if ( 0xf3 == prefix_repeat_opcode )
                {
                    while ( 0 != cx )
                    {
                        op_scas8();
                        cx--;
                        if ( !fZero )
                            break;
                    }
                }
                else
                    op_scas8();
                break;
            }
            case 0xaf: // scas16 compare ax with word at es:di
            {
                if ( 0xf2 == prefix_repeat_opcode )
                {
                    while ( 0 != cx )
                    {
                        op_scas16();
                        cx--;
                        if ( fZero )
                            break;
                    }
                }
                else if ( 0xf3 == prefix_repeat_opcode )
                {
                    while ( 0 != cx )
                    {
                        op_scas16();
                        cx--;
                        if ( !fZero )
                            break;
                    }
                }
                else
                    op_scas16();
                break;
            }
            case 0xc2: { ip = pop(); sp += _b12; continue; } // ret immed16 intrasegment
            case 0xc3: { ip = pop(); continue; } // ret intrasegment
            case 0xc4: // les reg16, [mem16]
            {
                _isword = true; // opcode is even, but it's a word.
                _bc++;
                uint16_t * preg = get_preg16( _reg );
                uint16_t * pvalue = (uint16_t *) get_rm_ptr( _rm );
                *preg = pvalue[ 0 ];
                es = pvalue[ 1 ];
                break;
            }
            case 0xc5: // lds reg16, [mem16]
            {
                _isword = true; // opcode is even, but it's a word.
                _bc++;
                uint16_t * preg = get_preg16( _reg );
                uint16_t * pvalue = (uint16_t *) get_rm_ptr( _rm );
                *preg = pvalue[ 0 ];
                ds = pvalue[ 1 ];
                break;
            }
            case 0xc6: // mov mem8, immed8
            {
                _bc++;
                uint8_t * pdst = (uint8_t *) get_rm_ptr( _rm );
                *pdst = _pcode[ _bc ];
                _bc++;
                break;
            }
            case 0xc7: // mov mem16, immed16
            {
                _bc++;
                uint16_t src;
                uint16_t * pdst = (uint16_t *) get_op_args( false, src );
                *pdst = src;
                break;
            }
            case 0xca: { ip = pop(); cs = pop(); sp += _b12; continue; } // retf immed16
            case 0xcb: { ip = pop(); cs = pop(); continue; } // retf
            case 0xcc: { DebugBreak(); break; } // int 3
            case 0xcd: // int
            {
                last_interrupt = _b1;
                uint32_t offset = 4 * _b1;
                uint16_t * vectorItem = (uint16_t *) ( memory + (uint32_t) 4 * _b1 );
                materializeFlags();
                push( flags );
                push( cs );
                push( ip + 2 );
                ip = vectorItem[ 0 ];
                cs = vectorItem[ 1 ];
                continue;
            }
            case 0xcf: // iret
            {
                ip = pop();
                cs = pop();
                flags = pop();
                unmaterializeFlags();
                continue;
            }
            case 0xd0: // bit shift reg8/mem8, 1
            {
                _bc++;
                uint8_t *pval = get_rm8_ptr();
                uint8_t original = *pval;

                if ( 0 == _reg ) op_rol8( pval, 1 );
                else if ( 1 == _reg ) op_ror8( pval, 1 );
                else if ( 2 == _reg ) op_rcl8( pval, 1 );
                else if ( 3 == _reg ) op_rcr8( pval, 1 );
                else if ( 4 == _reg ) op_sal8( pval, 1 ); // aka shr
                else if ( 5 == _reg ) op_shr8( pval, 1 );
                else if ( 6 == _reg ) { assert( false ); } // illegal
                else op_sar8( pval, 1 ); // ( 7 == _reg )
                break;
            }
            case 0xd1: // bit shift reg16/mem16, 1
            {
                _bc++;
                uint16_t *pval = get_rm16_ptr();
                uint16_t original = *pval;

                if ( 0 == _reg ) op_rol16( pval, 1 );
                else if ( 1 == _reg ) op_ror16( pval, 1 );
                else if ( 2 == _reg ) op_rcl16( pval, 1 );
                else if ( 3 == _reg ) op_rcr16( pval, 1 );
                else if ( 4 == _reg ) op_sal16( pval, 1 ); // aka shl
                else if ( 5 == _reg ) op_shr16( pval, 1 );
                else if ( 6 == _reg ) { assert( false ); } // illegal
                else  op_sar16( pval, 1 ); // ( 7 == _reg ) 
                break;
            }
            case 0xd2: // bit shift reg8/mem8, cl
            {
                _bc++;
                uint8_t *pval = get_rm8_ptr();
                uint8_t original = *pval;
                uint8_t amount = cl() & 0x1f;

                if ( 0 == _reg ) op_rol8( pval, amount );
                else if ( 1 == _reg ) op_ror8( pval, amount );
                else if ( 2 == _reg ) op_rcl8( pval, amount );
                else if ( 3 == _reg ) op_rcr8( pval, amount );
                else if ( 4 == _reg ) op_sal8( pval, amount ); // aka shl
                else if ( 5 == _reg ) op_shr8( pval, amount );
                else if ( 6 == _reg ) { assert( false ); } // illegal
                else op_sar8( pval, amount ); // ( 7 == _reg )
                break;
            }
            case 0xd3: // bit shift reg16/mem16, cl
            {
                _bc++;
                uint16_t *pval = get_rm16_ptr();
                uint16_t original = *pval;
                uint8_t amount = cl() & 0x1f;

                if ( 0 == _reg ) op_rol16( pval, amount );
                else if ( 1 == _reg ) op_ror16( pval, amount );
                else if ( 2 == _reg ) op_rcl16( pval, amount );
                else if ( 3 == _reg ) op_rcr16( pval, amount );
                else if ( 4 == _reg ) op_sal16( pval, amount ); // aka shl
                else if ( 5 == _reg ) op_shr16( pval, amount );
                else if ( 6 == _reg ) { assert( false ); } // illegal
                else op_sar16( pval, amount ); // ( 7 == _reg )
                break;
            }
            case 0xd4: // aam
            {
                _bc++;
                if ( 0 != _b1 )
                {
                    uint8_t tempal = al();
                    set_ah( tempal / _b1 );
                    set_al( tempal % _b1 );
                }
                break;
            }
            case 0xd5: // aad
            {
                uint8_t tempal = al();
                uint8_t tempah = ah();
                set_al( ( tempal + ( tempah * _b1 ) ) & 0xff );
                set_ah( 0 );
                _bc++;
                break;
            }
            case 0xd7: // xlat
            {
                uint8_t * ptable = flat_address8( get_seg_value( ds ), bx );
                set_al( ptable[ al() ] );
                break;
            }
            case 0xe0: // loopne short-label
            {
                cx--;
                _bc++;
                if ( 0 != cx && !fZero )
                {
                    ip += ( 2 + (int16_t) (int8_t) _b1 );
                    continue;
                }
                break;
            }
            case 0xe1: // loope short-label
            {
                cx--;
                _bc++;
                if ( 0 != cx && fZero )
                {
                    ip += ( 2 + (int16_t) (int8_t) _b1 );
                    continue;
                }
                break;
            }
            case 0xe2: // loop short-label
            {
                cx--;
                _bc++;
                if ( 0 != cx )
                {
                    ip += ( 2 + (int16_t) (int8_t) _b1 );
                    continue;
                }
                break;
            }
            case 0xe3: // jcxz rel8  jump if cx is 0
            {
                if ( 0 == cx )
                {
                    ip += ( 2 + (int16_t) (int8_t) _b1 );
                    continue;
                }
                _bc++;
                break;
            }
            case 0xe4: { set_al( 0 ); _bc++; break; } // in al, immed8
            case 0xe5: { ax = 0; _bc++; break; } // in ax, immed8
            case 0xe6: { _bc++; break; } // out al, immed8
            case 0xe7: { _bc++; break; } // out ax, immed8
            case 0xe8: // call a8
            {
                uint16_t return_address = ip + 3;
                push( return_address );
                ip = return_address + _b12;
                continue;
            }
            case 0xe9: { ip += ( 3 + (int16_t) _b12 ); continue; } // jmp near
            case 0xea: { ip = _b12; cs = _pcode[3] | ( uint16_t) _pcode[4] << 8; continue; } // jmp far
            case 0xeb: { ip += ( 2 + (int16_t) (int8_t) _b1 ); continue; } // jmp short i8
            case 0xec: { set_al( i8086_invoke_in( dx ) ); break; } // in al, dx
            case 0xed: { ax = 0; break; } // in ax, dx
            case 0xee: { break; } // out al, dx
            case 0xef: { break; } // out ax, dx
            case 0xf0: { break; } // lock prefix. ignore since interrupts won't happen
            case 0xf2: { prefix_repeat_opcode = _b0; ip++; goto _after_prefix; } // repne/repnz
            case 0xf3: { prefix_repeat_opcode = _b0; ip++; goto _after_prefix; } // rep/repe/repz
            case 0xf4: { i8086_invoke_halt(); goto _all_done; } // hlt
            case 0xf5: { fCarry = !fCarry; break; } //cmc
            case 0xf6: // test/UNUSED/not/neg/mul/imul/div/idiv r/m8
            {
                _bc++;

                if ( 0 == _reg )
                {
                    // test is different: reg8/mem8, immed8

                    uint8_t lhs = * (uint8_t *) get_rm_ptr( _rm );
                    uint8_t rhs = _pcode[ _bc++ ];
                    op_and8( lhs, rhs );
                }
                else if ( 2 == _reg ) // not reg8/mem8 -- no flags updated
                {
                    uint8_t * pval = (uint8_t *) get_rm_ptr( _rm );
                    *pval = ~ ( *pval );
                }
                else if ( 3 == _reg ) // neg reg8/mem8 (subtract from 0)
                {
                    uint8_t * pval = (uint8_t *) get_rm_ptr( _rm );
                    *pval = op_sub8( 0, *pval );
                }
                else if ( 4 == _reg ) // mul. ax = al * r/m8
                {
                    uint8_t rhs = * (uint8_t *) get_rm_ptr( _rm );
                    ax = (uint16_t) al() * (uint16_t) rhs;
                    set_PZS16( ax );
                    fCarry = fOverflow = false;
                }
                else if ( 5 == _reg ) // imul. ax = al * r/m8
                {
                    uint8_t rhs = * (uint8_t *) get_rm_ptr( _rm );
                    uint32_t result = (int16_t) al() * (int16_t) rhs;
                    ax = result & 0xffff;
                    set_PZS16( ax );
                    result &= 0xffff8000;
                    fCarry = fOverflow = ( ( 0 != result ) && ( 0xffff8000 != result ) );
                }
                else if ( 6 == _reg ) // div m, r8 / src. al = result, ah = remainder
                {
                    uint8_t rhs = * get_preg8( _rm );
                    if ( 0 != rhs )
                    {
                        uint16_t lhs = ax;
                        set_al( (uint8_t) ( lhs / (uint16_t) rhs ) );
                        set_ah( lhs % rhs );
                        set_PZS8( al() );
                    }
                }
                else if ( 7 == _reg ) // idiv r/m8
                {
                    uint8_t rhs = * (uint8_t *) get_rm_ptr( _rm );
                    int16_t lhs = ax;
                    set_al( ( lhs / (int16_t) rhs ) & 0xff );
                    set_ah( lhs % (int16_t) rhs );
                    set_PZS8( al() );
                }
                else
                    assert( false );

                break;
            }
            case 0xf7: // test/UNUSED/not/neg/mul/imul/div/idiv r/m16
            {
                _bc++;

                if ( 0 == _reg )
                {
                    // test is different: reg16/mem16, immed16

                    uint16_t lhs = * (uint16_t *) get_rm_ptr( _rm );
                    uint16_t rhs = _pcode[ _bc++ ];
                    rhs |= ( (uint16_t) ( _pcode[ _bc++ ] ) << 8 );
                    op_and16( lhs, rhs );
                }
                else if ( 2 == _reg ) // not reg16/mem16 -- no flags updated
                {
                    uint16_t * pval = (uint16_t *) get_rm_ptr( _rm );
                    *pval = ~ ( *pval );
                }
                else if ( 3 == _reg ) // neg reg16/mem16 (subtract from 0)
                {
                    uint16_t * pval = (uint16_t *) get_rm_ptr( _rm );
                    *pval = op_sub16( 0, *pval );
                }
                else if ( 4 == _reg ) // mul. dx:ax = ax * src
                {
                    uint16_t rhs = * (uint16_t *) get_rm_ptr( _rm );
                    uint32_t result = (uint32_t) ax * (uint32_t) rhs;
                    dx = result >> 16;
                    ax = result & 0xffff;
                    set_PZS16( ax );
                    fCarry = fOverflow = ( result > 0xffff );
                }
                else if ( 5 == _reg ) // imul. dx:ax = ax * src
                {
                    uint16_t rhs = * (uint16_t *) get_rm_ptr( _rm );
                    uint32_t result = (int32_t) ax * (int32_t) rhs;
                    dx = result >> 16;
                    ax = result & 0xffff;
                    set_PZS16( ax );
                    result &= 0xffff8000;
                    fCarry = fOverflow = ( ( 0 != result ) && ( 0xffff8000 != result ) );
                }
                else if ( 6 == _reg ) // div dx:ax / src. ax = result, dx = remainder
                {
                    uint16_t rhs = * (uint16_t *) get_rm_ptr( _rm );
                    if ( 0 != rhs )
                    {
                        uint32_t lhs = ( (uint32_t) dx << 16 ) + (uint32_t) ax;
                        ax = (uint16_t) ( lhs / (uint32_t) rhs );
                        dx = lhs % rhs;
                        set_PZS16( ax );
                    }
                }
                else if ( 7 == _reg ) // idiv dx:ax / src. ax = result, dx = remainder
                {
                    uint16_t rhs = * (uint16_t *) get_rm_ptr( _rm );
                    if ( 0 != rhs )
                    {
                        uint32_t lhs = ( (uint32_t) dx << 16 ) + (uint32_t) ax;
                        ax = (uint16_t) ( (int32_t) lhs / (int32_t) (int16_t) rhs );
                        dx = (int32_t) lhs % (int32_t) rhs;
                        set_PZS16( ax );
                    }
                }
                else
                {
                    printf( "_reg math not implemented: %d\n", _reg );
                    assert( false );
                }

                break;
            }
            case 0xf8: { fCarry = false; break; } // clc
            case 0xf9: { fCarry = true; break; } // stc
            case 0xfa: { fInterrupt = false; break; } // cli
            case 0xfb: { fInterrupt = true; break; } // sti
            case 0xfc: { fDirection = false; break; } // cld
            case 0xfd: { fDirection = true; break; } // std
            case 0xfe: // inc/dec reg8/mem8
            {
                _bc++;
                uint8_t * pdst = (uint8_t *) get_rm_ptr( _rm );

                if ( 0 == _reg ) // inc
                    *pdst = op_inc8( *pdst );
                else
                    *pdst = op_dec8( *pdst );
                break;
            }
            case 0xff: // many
            {
                if ( 0 == _reg ) // inc mem16
                {
                    uint16_t * pval = (uint16_t *) get_rm_ptr( _rm );
                    *pval = op_inc16( *pval );
                    _bc++;
                }
                else if ( 1 == _reg ) // dec mem16
                {
                    uint16_t * pval = (uint16_t *) get_rm_ptr( _rm );
                    *pval = op_dec16( *pval );
                    _bc++;
                }
                else if ( 2 == _reg ) // call reg16/mem16 (intra segment)
                {
                    uint16_t * pfunc = (uint16_t *) get_rm_ptr( _rm );
                    uint16_t return_address = ip + _bc + 1;
                    push( return_address );
                    ip = *pfunc;
                    continue;
                }
                else if ( 3 == _reg ) // call mem16:16 (inter segment)
                {
                    uint16_t * pdata = (uint16_t *) get_rm_ptr( _rm );
                    push( cs );
                    push( ip + _bc + 1 );
                    ip = pdata[ 0 ];
                    cs = pdata[ 1 ];
                    continue;
                }
                else if ( 4 == _reg ) // jmp reg16/mem16 (intra segment)
                {
                    ip = * (uint16_t *) get_rm_ptr( _rm );
                    continue;
                }
                else if ( 5 == _reg ) // jmp mem16 (inter segment)
                {
                    uint16_t * pdata = (uint16_t *) get_rm_ptr( _rm );
                    ip = pdata[ 0 ];
                    cs = pdata[ 1 ];
                    continue;
                }
                else if ( 6 == _reg ) // push mem16
                {
                    uint16_t * pval = (uint16_t *) get_rm_ptr( _rm );
                    push( *pval );
                    _bc++;
                }

                break;
            }
            default:
                handled = false;
        }

        if ( !handled )
        {
            handled = true;

            if ( _b0 >= 0x40 && _b0 <= 0x47 ) // inc ax..di
            {
                uint16_t *pval = get_preg16( _b0 - 0x40 );
                *pval = op_inc16( *pval );
            }
            else if ( _b0 >= 0x48 && _b0 <= 0x4f ) // dec ax..di
            {
                uint16_t *pval = get_preg16( _b0 - 0x40 );
                *pval = op_dec16( *pval );
            }
            else if ( _b0 >= 0x50 && _b0 <= 0x5f )  // push / pop
            {
                uint16_t * preg = get_preg16( _b0 & 7 );
                if ( _b0 <= 0x57 )
                    push( *preg );
                else 
                    *preg = pop();
            }
            else if ( _b0 >= 0x70 && _b0 <= 0x7f )  // jcc
            {
                _bc = 2;
                uint8_t jmp = _b0 & 0xf;
                bool takejmp = false;

                switch( jmp )
                {
                    case 0:  takejmp = fOverflow; break;                         // jo
                    case 1:  takejmp = !fOverflow; break;                        // jno
                    case 2:  takejmp = fCarry; break;                            // jb / jnae / jc
                    case 3:  takejmp = !fCarry; break;                           // jnb / jae / jnc
                    case 4:  takejmp = fZero; break;                             // je / jz
                    case 5:  takejmp = !fZero; break;                            // jne / jnz
                    case 6:  takejmp = fCarry || fZero; break;                   // jbe / jna
                    case 7:  takejmp = !fCarry && !fZero; break;                 // jnbe / ja
                    case 8:  takejmp = fSign; break;                             // js
                    case 9:  takejmp = !fSign; break;                            // jns
                    case 10: takejmp = fParityEven; break;                       // jp / jpe
                    case 11: takejmp = !fParityEven; break;                      // jnp / jpo
                    case 12: takejmp = ( fSign != fOverflow ); break;            // jl / jnge
                    case 13: takejmp = ( fSign == fOverflow ); break;            // jnl / jge
                    case 14: takejmp = fZero || ( fSign != fOverflow ); break;   // jle / jng
                    case 15: takejmp = !fZero && ( fSign == fOverflow  ); break; // jnle / jg
                }

                if ( takejmp )
                {
                    ip += ( 2 + (int) (char) _b1 );
                    continue;
                }
            }
            else if ( _b0 >= 0xb0 && _b0 <= 0xbf ) // mov r, immed
            {
                if ( _b0 <= 0xb7 )
                {
                    * get_preg8( _b0 & 7 ) = _b1;
                    _bc = 2;
                }
                else
                {
                    * get_preg16( 8 + ( _b0 & 7 ) ) = _b12;
                    _bc = 3;
                }
            }
            else if ( _b0 >= 0x91 && _b0 <= 0x97 ) // xchg ax, cx/dx/bx/sp/bp/si/di  0x90 is nop
            {
                uint16_t * preg = get_preg16( _b0 & 7 );
                swap( ax, * preg );
            }
            else
                handled = false;
        }

        if ( !handled )
        {
            byte top6 = _b0 & 0xfc;
            _bc = 2;

            switch( top6 )
            {
                case 0x00: // add
                {
                    uint16_t src;
                    void * pdst = get_op_args( true, src );
                    if ( _isword )
                        * (uint16_t *) pdst = op_add16( * (uint16_t *) pdst, src );
                    else
                        * (uint8_t *) pdst = op_add8( * (uint8_t *) pdst, src & 0xff );
                    break;
                }
                case 0x08: // or
                {
                    uint16_t src;
                    void * pdst = get_op_args( true, src );
                    if ( _isword )
                        * (uint16_t *) pdst = op_or16( * (uint16_t *) pdst, src );
                    else
                        * (uint8_t *) pdst = op_or8( * (uint8_t *) pdst, src & 0xff );
                    break;
                }
                case 0x10: // adc
                {
                    uint16_t src;
                    void * pdst = get_op_args( true, src );
                    if ( _isword )
                        * (uint16_t *) pdst = op_add16( * (uint16_t *) pdst, src, fCarry );
                    else
                        * (uint8_t *) pdst = op_add8( * (uint8_t *) pdst, src & 0xff, fCarry );
                    break;
                }
                case 0x18: // sbb
                {
                    uint16_t src;
                    void * pdst = get_op_args( true, src );
                    if ( _isword )
                        * (uint16_t *) pdst = op_sub16( * (uint16_t *) pdst, src, fCarry );
                    else
                        * (uint8_t *) pdst = op_sub8( * (uint8_t *) pdst, src & 0xff, fCarry );
                    break;
                }
                case 0x20: // and
                {
                    uint16_t src;
                    void * pdst = get_op_args( true, src );
                    if ( _isword )
                        * (uint16_t *) pdst = op_and16( * (uint16_t *) pdst, src );
                    else
                        * (uint8_t *) pdst = op_and8( * (uint8_t *) pdst, src & 0xff );
                    break;
                }
                case 0x28: // sub
                {
                    uint16_t src;
                    void * pdst = get_op_args( true, src );
                    if ( _isword )
                        * (uint16_t *) pdst = op_sub16( * (uint16_t *) pdst, src );
                    else
                        * (uint8_t *) pdst = op_sub8( * (uint8_t *) pdst, src & 0xff );
                    break;
                }
                case 0x30: // xor
                {
                    uint16_t src;
                    void * pdst = get_op_args( true, src );
                    if ( _isword )
                        * (uint16_t *) pdst = op_xor16( * (uint16_t *) pdst, src );
                    else
                        * (uint8_t *) pdst = op_xor8( * (uint8_t *) pdst, src & 0xff );
                    break;
                }
                case 0x38: // cmp
                {
                    uint16_t src;
                    void * pdst = get_op_args( true, src );
                    if ( _isword )
                        op_sub16( * (uint16_t *) pdst, src );
                    else
                        op_sub8( * (uint8_t *) pdst, (uint8_t) src );
                    break;
                }
                case 0x80: // math
                {
                    uint8_t math = _reg; // the _reg field is the math operator, not a register
                    _bc++;

                    bool directAddress = ( 0 == _mod && 6 == _rm );
                    int immoffset = 2;
                    if ( 1 == _mod )
                        immoffset += 1;
                    else if ( 2 == _mod || directAddress )
                        immoffset += 2;

                    if ( _isword )
                    {
                        uint16_t rhs;
                        if ( 0x83 == _b0 ) // one byte immediate, word math
                            rhs = (int8_t) _pcode[ immoffset ]; // cast for sign extension from byte to word
                        else
                        {
                            _bc++;
                            rhs = (uint16_t) _pcode[ immoffset ] + ( (uint16_t) ( _pcode[ 1 + immoffset ] ) << 8 );
                        }

                        do_math16( math, get_rm16_ptr(), rhs );
                    }
                    else
                    {
                        uint8_t rhs = _pcode[ immoffset ];
                        do_math8( math, get_rm8_ptr(), rhs );
                    }
                    break;
                }
                default:
                {
                    tracer.Trace( "unhandled instruction %02x\n", _b0 );
                    printf( "unhandled instruction %02x\n", _b0 );
                    exit( 1 );
                }
            }
        }

        ip += _bc;
    }
_all_done:
    return cycles;
} //emulate
