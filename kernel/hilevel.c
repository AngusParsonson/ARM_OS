/* Copyright (C) 2017 Daniel Page <csdsp@bristol.ac.uk>
 *
 * Use of this source code is restricted per the CC BY-NC-ND license, a copy of
 * which can be found via http://creativecommons.org (and should be included as
 * LICENSE.txt within the associated archive or repository).
 */

#include "hilevel.h"
#define SIZE_OF_STACK 0x00001000
#define maximum_number_of_PCBs 64

int number_of_programs = 1;
pcb_t pcb[ maximum_number_of_PCBs ]; pcb_t* current = NULL;

void dispatch( ctx_t* ctx, pcb_t* prev, pcb_t* next ) {
  char prev_pid = '?', next_pid = '?';

  if( NULL != prev ) {
    memcpy( &prev->ctx, ctx, sizeof( ctx_t ) ); // preserve execution context of P_{prev}
    prev_pid = '0' + prev->pid;
  }
  if( NULL != next ) {
    memcpy( ctx, &next->ctx, sizeof( ctx_t ) ); // restore  execution context of P_{next}
    next_pid = '0' + next->pid;
  }

    PL011_putc( UART0, '[',      true );
    PL011_putc( UART0, prev_pid, true );
    PL011_putc( UART0, '-',      true );
    PL011_putc( UART0, '>',      true );
    PL011_putc( UART0, next_pid, true );
    PL011_putc( UART0, ']',      true );

    current = next;                             // update   executing index   to P_{next}

  return;
}

void schedule( ctx_t* ctx ) {
  current->age = 0; // Sets the age of the previously ran program to 0
  uint32_t next_program = 0;

  for (int i = 0; i < number_of_programs; i++) { // Loops through programs looking for the one with highest age
    if (pcb[i].age >= pcb[next_program].age && pcb[i].status != STATUS_TERMINATED) next_program = i;
  }

  // Ensures terminated programs are never reset to ready
  if ( current->status != STATUS_TERMINATED )  current->status = STATUS_READY; // Sets the status to the previously ran program to READY


  pcb[ next_program ].status = STATUS_EXECUTING; // Updates status of new program to run

  for (int i = 0; i < number_of_programs; i++) { // Updates all ages for programs based on their priorities
    if ( i != next_program ) pcb[i].age += pcb[i].priority;
  }

  dispatch( ctx, current, &pcb[ next_program ] ); // Dispatches new program for running

  return;
}

extern void main_console();
extern uint32_t tos_programs;

void hilevel_handler_rst( ctx_t* ctx ) {

  /* Initialise two PCBs, representing user processes stemming from execution
   * of two user programs.  Note in each case that
   *
   * - the CPSR value of 0x50 means the processor is switched into USR mode,
   *   with IRQ interrupts enabled, and
   * - the PC and SP values matche the entry point and top of stack.
   */
  memset( &pcb[ 0 ], 0, sizeof( pcb_t ) );     // initialise 0-th PCB = console
  pcb[ 0 ].pid      = 1;
  pcb[ 0 ].status   = STATUS_CREATED;
  pcb[ 0 ].ctx.cpsr = 0x50;
  pcb[ 0 ].ctx.pc   = ( uint32_t )( &main_console );
  pcb[ 0 ].ctx.sp   = ( uint32_t )( &tos_programs );
  pcb[ 0 ].tos      = ( uint32_t )( &tos_programs );
  pcb[ 0 ].priority = 1;
  pcb[ 0 ].age      = 0;

  TIMER0->Timer1Load  = 0x00100000; // select period = 2^20 ticks ~= 1 sec
  TIMER0->Timer1Ctrl  = 0x00000002; // select 32-bit   timer
  TIMER0->Timer1Ctrl |= 0x00000040; // select periodic timer
  TIMER0->Timer1Ctrl |= 0x00000020; // enable          timer interrupt
  TIMER0->Timer1Ctrl |= 0x00000080; // enable          timer

  GICC0->PMR          = 0x000000F0; // unmask all            interrupts
  GICD0->ISENABLER1  |= 0x00000010; // enable timer          interrupt
  GICC0->CTLR         = 0x00000001; // enable GIC interface
  GICD0->CTLR         = 0x00000001; // enable GIC distributor

  int_enable_irq();
  PL011_putc( UART0, 'R', true);

  /* Once the PCBs are initialised, we arbitrarily select the one in the 0-th
   * PCB to be executed: there is no need to preserve the execution context,
   * since it is is invalid on reset (i.e., no process will previously have
   * been executing).
   */

  dispatch( ctx, NULL, &pcb[ 0 ] );

  return;
}

void hilevel_handler_irq(ctx_t* ctx) {
  // Step 2: read  the interrupt identifier so we know the source.
  uint32_t id = GICC0->IAR;
  if( id == GIC_SOURCE_TIMER0 ) {
    // Step 4: handle the interrupt, then clear (or reset) the source.
    TIMER0->Timer1IntClr = 0x01;
    schedule(ctx);
  }
  // Step 5: write the interrupt identifier to signal we're done.
  GICC0->EOIR = id;

  return;
}

void hilevel_handler_svc( ctx_t* ctx, uint32_t id ) {
  /* Based on the identifier (i.e., the immediate operand) extracted from the
   * svc instruction,
   *
   * - read  the arguments from preserved usr mode registers,
   * - perform whatever is appropriate for this system call, then
   * - write any return value back to preserved usr mode registers.
   */

  switch( id ) {
    case 0x00 : { // 0x00 => yield()
      schedule( ctx );

      break;
    }

    case 0x01 : { // 0x01 => write( fd, x, n )
      int   fd = ( int   )( ctx->gpr[ 0 ] );
      char*  x = ( char* )( ctx->gpr[ 1 ] );
      int    n = ( int   )( ctx->gpr[ 2 ] );

      for( int i = 0; i < n; i++ ) {
        PL011_putc( UART0, *x++, true );
      }

      ctx->gpr[ 0 ] = n;

      break;
    }

    case 0x03: { // 0x03 => fork()
      memcpy( &pcb[ number_of_programs ].ctx, ctx, sizeof(ctx_t) ); // Copies context

      pcb[ number_of_programs ].pid      = number_of_programs + 1;
      pcb[ number_of_programs ].status   = STATUS_CREATED;
      pcb[ number_of_programs ].priority = current->priority;
      pcb[ number_of_programs ].age      = current->age;
      pcb[ number_of_programs ].ctx.pc   = ctx->pc; // Sets program counter to ensure program returns to the correct spot
      pcb[ number_of_programs ].tos      = ( uint32_t )(&tos_programs) - (number_of_programs * SIZE_OF_STACK); // Sets the top of stack for the new program according to stack size

      uint32_t offset = current->tos - ctx->sp; // Calculates offset based on where the current stack pointer is
      pcb[ number_of_programs ].ctx.sp   = pcb[ number_of_programs ].tos - offset; // Assigns stack pointer
      memcpy( (void*)(pcb[ number_of_programs ].tos - SIZE_OF_STACK), (void*)(current->tos - SIZE_OF_STACK), SIZE_OF_STACK); // Copies stack

      pcb[ number_of_programs ].ctx.gpr[ 0 ] = 0; // Places 0 in general purpose register [ 0 ] for child
      ctx->gpr[ 0 ] = number_of_programs + 1; // Places another arbitrary value in general purpose register [ 0 ] for parent

      number_of_programs++;
      break;
    }

    case 0x04: { // 0x05 => exit( x )
      uint32_t program_to_terminate = ( uint32_t )( current->pid ); // Finds current program (which called exit)
      pcb[ program_to_terminate - 1 ].status = STATUS_TERMINATED; // Terminates program

      schedule(ctx); // Essentially yeilds so the terminated program is exited
      break;
    }

    case 0x05: { // 0x05 => exec( x )
      ctx->pc   = ( uint32_t )( ctx->gpr[ 0 ] ); // Sets program counter to the correct place in memory for that program i.e. P3 or P4 etc.
      ctx->sp   = current->tos; // Sets the stack pointer to the top of the stack
      
      break;
    }

    case 0x06: { // 0x05 => kill( pid, x )
      uint32_t program_to_terminate = ( uint32_t )( ctx->gpr[ 0 ] ); // Finds program which needs terminating in general purpose register [ 0 ]
      pcb[ program_to_terminate - 1 ].status = STATUS_TERMINATED; // Terminates program

      break;
    }

    case 0x07: { // 0x05 => nice( pid, priority )
      uint32_t program_to_update = ( uint32_t )( ctx->gpr[ 0 ] ); // Finds program which needs updating in general purpose register [ 0 ]
      uint32_t new_priority = ( uint32_t )( ctx->gpr[1] ); // Finds the new requested priority in general purpose register [ 1 ]

      pcb[ program_to_update - 1 ].priority = new_priority; // Updates priority

      break;
    }

    default   : { // 0x?? => unknown/unsupported
      break;
    }
  }

  return;
}
