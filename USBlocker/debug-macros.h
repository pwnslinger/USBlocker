
#ifndef __DEBUG_MACROS_H__
#define __DEBUG_MACROS_H__



#ifdef DBG

#define DEBUG_ENTER_FUNCTION(aFormat, ...)	\
	DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_TRACE_LEVEL, "[%u:%u:%u]: " __FILE__ ": " __FUNCTION__ "(" aFormat ")\n", PsGetCurrentProcessId(), PsGetCurrentThreadId(), KeGetCurrentIrql(), __VA_ARGS__)

#define DEBUG_ENTER_FUNCTION_NO_ARGS()	\
	DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_TRACE_LEVEL, "[%u:%u:%u]: " __FILE__ ": " __FUNCTION__ "()\n", PsGetCurrentProcessId(), PsGetCurrentThreadId(), KeGetCurrentIrql())

#define DEBUG_EXIT_FUNCTION(aFormat, ...)	\
	DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_TRACE_LEVEL, "[%u:%u:%u]: " __FILE__ ": " __FUNCTION__ "(-):" aFormat "\n", PsGetCurrentProcessId(), PsGetCurrentThreadId(), KeGetCurrentIrql(), __VA_ARGS__)

#define DEBUG_EXIT_FUNCTION_VOID()	\
	DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_TRACE_LEVEL, "[%u:%u:%u]: " __FILE__ ": " __FUNCTION__ "(-)\n", PsGetCurrentProcessId(), PsGetCurrentThreadId(), KeGetCurrentIrql())

#define DEBUG_MSG(aFormat, ...)	\
	DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_TRACE_LEVEL, "[%u:%u:%u]: " __FILE__ ": " __FUNCTION__ ": " aFormat "\n", PsGetCurrentProcessId(), PsGetCurrentThreadId(), KeGetCurrentIrql(), __VA_ARGS__)

#else

#define DEBUG_ENTER_FUNCTION(aFormat, ...)		{	}
#define DEBUG_ENTER_FUNCTION_NO_ARGS()			{	}
#define DEBUG_EXIT_FUNCTION(aFormat, ...)		{	}
#define DEBUG_EXIT_FUNCTION_VOID()				{	}
#define DEBUG_EXIT_FUNCTION_VOID()				{	}
#define DEBUG_MSG(aFormat, ...)					{	}

#endif






#endif
