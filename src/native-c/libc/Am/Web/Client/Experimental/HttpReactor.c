// Explicit unsupported backend; Tucanium keeps its worker pool on these targets.
#include <libc/core.h>
#include <libc/core_inline_functions.h>
#include <Am/Web/Client/Experimental/HttpReactor.h>
static function_result unsupported(void) {
 function_result r={0};__throw_simple_exception("HTTP reactor is currently Mac ARM only","HttpReactor",&r);return r;
}
function_result Am_Web_Client_Experimental_HttpReactor__native_init_0(aobject * const self) {return unsupported();}
function_result Am_Web_Client_Experimental_HttpReactor__native_release_0(aobject * const self) {function_result r={0};return r;}
function_result Am_Web_Client_Experimental_HttpReactor__native_mark_children_0(aobject * const self) {function_result r={0};return r;}
function_result Am_Web_Client_Experimental_HttpReactor_closeNative_0(aobject * const self) {function_result r={0};return r;}
function_result Am_Web_Client_Experimental_HttpReactor_submitNative_0(aobject * const self,aobject *host,int port,aobject *path,bool tls,int timeout) {return unsupported();}
function_result Am_Web_Client_Experimental_HttpReactor_ready_0(aobject * const self,int id) {return unsupported();}
function_result Am_Web_Client_Experimental_HttpReactor_waitAny_0(aobject * const self,int timeout) {return unsupported();}
function_result Am_Web_Client_Experimental_HttpReactor_cancel_0(aobject * const self,int id) {return unsupported();}
function_result Am_Web_Client_Experimental_HttpReactor_takeRaw_0(aobject * const self,int id) {return unsupported();}

function_result Am_Web_Client_Experimental_HttpReactor_setLimitNative_0(aobject * const self,int limit) {return unsupported();}

function_result Am_Web_Client_Experimental_HttpReactor_getMaxThreads_0(void) {return unsupported();}
function_result Am_Web_Client_Experimental_HttpReactor_setMaxThreads_0(int maximum) {(void)maximum;return unsupported();}
function_result Am_Web_Client_Experimental_HttpReactor_workerCount_0(void) {return unsupported();}
