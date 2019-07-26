/*------------------------------------------------------------------------------*
 * File Name:				 													*
 * Creation: 																	*
 * Purpose: OriginC Source C file												*
 * Copyright (c) ABCD Corp.	2003, 2004, 2005, 2006, 2007, 2008, 2009, 2010		*
 * All Rights Reserved															*
 * 																				*
 * Modification Log:															*
 *------------------------------------------------------------------------------*/


#include "stdafx.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "C:\Users\Ben\AppData\Local\Programs\Python\Python38\include\Python.h"


char* GetDirectoryFromFullPath(const char fp[]);
char* StrReplace(char* orig, const char* rep, const char* with);
PyObject* _RunPythonFunction(PyObject* pModule, char* py_fun_name, PyObject* pyInput[], int num_args, int error_code);
PyObject* __RunPythonFunction(PyObject* pModule, PyObject* pFunc, PyObject* pyInput[], int num_args, int error_code);

/**************************************************************************************************
	Error handling
**************************************************************************************************/
enum {
	PYTHON_C_CANNOT_CONVERT_ARGUMENT_TO_PY, // Cannot convert argument
	PYTHON_C_PY_CALL_FALLIED, // Call failed
	PYTHON_C_CANNOT_FIND_PY_FUNCTION, // Cannot find function
	PYTHON_C_PY_MODULE_NOT_LOADED // Module not loaded
};

void Python_C_PrintErrorCodeText(int error_code)
{
	switch( error_code ) {
	case PYTHON_C_CANNOT_CONVERT_ARGUMENT_TO_PY:
		printf("Cannot convert argument to Python object\n");
		break;
	case PYTHON_C_PY_CALL_FALLIED:
		printf("Call to Python function failed\n");
		break;
	case PYTHON_C_CANNOT_FIND_PY_FUNCTION:
		printf("Cannot find Python function\n");
		break;
	case PYTHON_C_PY_MODULE_NOT_LOADED:
		printf("Python module not loaded\n");
	}
}


/**************************************************************************************************
	Type conversion of C primitives & arrays to Python
	 - ToPyType(inputVar) for primitives, 
	 - ToPyType(inputVar, size_t) for arrays
	 - retrns Python ojbect or NULL on failure
	 - intenger types are converted to Pyong Long objects, floating types to Python float objects
		and char srtings to Python strings
**************************************************************************************************/
#define TO_PYTHON_LIST(TYPE, CONVERT_FUN) 							\
static PyObject* ToPyType(TYPE &x) { return CONVERT_FUN(x); }		\
static PyObject* ToPyType(TYPE *arr, Py_ssize_t nSize) {				\
	PyObject *pyList;												\
	pyList = PyList_New(nSize);										\
	for(Py_ssize_t ii = 0; ii < nSize; ii++) {						\
		PyList_SetItem(pyList, ii, CONVERT_FUN(*arr++));			\
	}																\
	return pyList; 													\
}

TO_PYTHON_LIST(bool, PyBool_FromLong)
TO_PYTHON_LIST(int, PyLong_FromLong)
TO_PYTHON_LIST(unsigned int, PyLong_FromSize_t)
#ifdef _OWIN64
TO_PYTHON_LIST(Py_ssize_t, PyLong_FromSsize_t)
#endif
TO_PYTHON_LIST(short, PyLong_FromLong)
TO_PYTHON_LIST(unsigned short, PyLong_FromUnsignedLong)
//TO_PYTHON_LIST(long, PyLong_FromLong)
//TO_PYTHON_LIST(unsigned long, PyLong_FromUnsignedLong)
TO_PYTHON_LIST(double, PyFloat_FromDouble)
TO_PYTHON_LIST(float, PyFloat_FromDouble)
//TO_PYTHON_LIST(LongDoubleArray_ToPyList, long double)
static PyObject* ToPyType(char *x) { return PyUnicode_DecodeFSDefault(x); }
static PyObject *ToPyType(char *arr[], int nSize) {
	PyObject *pyList;
	pyList = PyList_New(nSize);
	for(Py_ssize_t ii = 0; ii < nSize; ii++) {
		PyList_SetItem(pyList, ii, PyUnicode_DecodeFSDefault(*arr++));
	}
	return pyList;
}


/**************************************************************************************************
	Type conversion of Python objecs to C types
	 - FromPyType(PyObject *, inputVar) for primitives, 
	 - FromPyType(PyObject *, inputVar, size_t) for arrays
	 - returns false on failure to convert
	 - Python float must be converted to double or float, Python 'long' must be converted to an 
		integer type and Python string must be converted to char[]
**************************************************************************************************/
#define FROM_PYTHON_TYPE(TYPE, CONVERT_FUN) 							\
static bool FromPyType(PyObject *pyObj, TYPE& x) {						\
	x = CONVERT_FUN(pyObj);												\
	/* if pyObj cannot be converted, returns '-1' */					\
	if(x == (TYPE)-1 && !PyErr_Occurred()) {							\
		return false;													\
	}																	\
	return true;														\
}																		\
static bool FromPyType(PyObject *pyList, TYPE* x, Py_ssize_t nSize) {	\
	for(Py_ssize_t ii = 0; ii < nSize; ii++) {							\
		*x = CONVERT_FUN(PyList_GetItem(pyList, ii));					\
		if(*x == (TYPE)-1 && !PyErr_Occurred()) {						\
			return false;												\
		}																\
		++x;															\
	}																	\
	return true; 														\
}

FROM_PYTHON_TYPE(int, _PyLong_AsInt) /* '_PyLong_AsInt()' is not in online documentation, refer to python include file "longobject.h" for more info */
FROM_PYTHON_TYPE(unsigned int, PyLong_AsSize_t) 
//FROM_PYTHON_TYPE(Py_ssize_t, PyLong_AsSize_t) 
//FROM_PYTHON_TYPE(short, PyLong_AsLong)
//FROM_PYTHON_TYPE(unsigned short, PyLong_AsUnsignedLong)
FROM_PYTHON_TYPE(long, PyLong_AsLong)
FROM_PYTHON_TYPE(unsigned long, PyLong_AsUnsignedLong)
FROM_PYTHON_TYPE(double, PyFloat_AsDouble)
//FROM_PYTHON_TYPE(float, PyFloat_AsDouble)

// char type requires different error checking
static bool FromPyType(PyObject *pyObj, const char *x) {
	x = PyUnicode_AsUTF8(pyObj);
	return x != NULL;
}
static bool FromPyType(PyObject *pyList, const char *x[], Py_ssize_t nSize) {
	for(Py_ssize_t ii = 0; ii < nSize; ii++) {
		*x = PyUnicode_AsUTF8(PyList_GetItem(pyList, ii));
		if(*x == NULL) {
			return false;									
		}
		++x;
	}
	return true;
}

// use the following to get the length, 'len', of char array(s) as well
static bool CharFromPyTypeAndSize(PyObject *pyObj, const char *x, Py_ssize_t *len) {
	x = PyUnicode_AsUTF8AndSize(pyObj, len);
	return x != NULL;
}
static bool CharFromPyTypeAndSize(PyObject *pyList, const char *x[], Py_ssize_t *len[], Py_ssize_t nSize) {
	for(Py_ssize_t ii = 0; ii < nSize; ii++) {
		*x = PyUnicode_AsUTF8AndSize(PyList_GetItem(pyList, ii), len[ii]);
		if(*x == NULL) {
			return false;									
		}
		++x;
	}
	return true;
}


/**************************************************************************************************
	Interpreter controls
**************************************************************************************************/
// initilize Python interpreter, returns true on success
bool PyInit()
{
	if(!Py_IsInitialized())
	{
		Py_Initialize();
		return Py_IsInitialized();
	}
	else
		return true;
}

// close Python interpreter, returns true on success
bool PyClose() 
{
	Py_Finalize();
	return !Py_IsInitialized();
}

// Python looks in sys.path list when importing modules, this will add 'dir' to the sys.path list
// this is usefull if you want your py files to travel with the code/exe rather than the default Python directory
void AddDirectoryToModuleImport(char* dir)
{
	size_t pyCode_len;
	char* dir_ex;
	char* pyCode;
	const char* py_start = "import sys\nsys.path.insert(0, '"; // length = 32
	const char* py_end = "')"; // length = 2
	
	if (!dir)
		return;
	
	dir_ex = StrReplace(dir, "\\", "\\\\"); // '\' will escape in Python as well
	
	pyCode_len = strlen(dir_ex) + strlen(py_start) + strlen(py_end) +1;
	pyCode = (char*)malloc(pyCode_len );
	if (!pyCode || pyCode_len < 34) {
		return;
	}

	/* prepare Python code to add dir to sys.path */
	pyCode = strcpy(pyCode, py_start);
	strcat(pyCode, dir_ex);
	strcat(pyCode, py_end);
	

	PyRun_SimpleString(pyCode);
	
	free(pyCode);
	free(dir_ex);
	
	/* dev note, it would be good to add a check that dir has been added */
}

void AddWorkingDirectoryToModuleImport()
{
	char fileName[] = __FILE__;
	char* dir = GetDirectoryFromFullPath(fileName);
	AddDirectoryToModuleImport(dir);
	free(dir);
}

// import module by filename (excluding the '.py')
// returns a pointer to the module which must be decremented when finished, returns NULL on failure
PyObject* ImportPyModule(char* module_name)
{
	PyObject *pModule;
	
	pModule = PyImport_ImportModule(module_name);
	
	//PyObject *pName
	//pName = PyUnicode_DecodeFSDefault(module_name);
	//pModule = PyImport_Import(pName);
	//Py_DecRef(pName);
	
	return pModule;
}

// get a PyObject representation of a function
PyObject* GetPyFunction(PyObject* pModule, char* py_fun_name)
{
	PyObject *pFunc;
	
	pFunc = PyObject_GetAttrString(pModule, py_fun_name);
	
	if (!pFunc) {
		printf("Cannot find function\n");
	}
	else if(!PyCallable_Check(pFunc)) {
		printf("Unable to call function\n");
		Py_DecRef(pFunc);
	}
	
	return pFunc;
}

// loads module, gets function, calls it and returns result
PyObject* RunPythonFunction(char *module_name, char *py_fun_name, PyObject *pyInput[], int num_args, int error_code)
{
	PyObject *pModule, *pResult;
	pModule = ImportPyModule(module_name);
	pResult = _RunPythonFunction(pModule, py_fun_name, pyInput, num_args, error_code);
	if(pModule)
	{
		
		Py_DecRef(pModule);
	}
	else {
		printf("Module not loaded\n");
	}
	
	return pResult;
}
// gets function, calls it and returns result
PyObject* _RunPythonFunction(PyObject *pModule, char *py_fun_name, PyObject *pyInput[], int num_args, int error_code)
{
	PyObject *pFunc, *pResult = NULL;
	
	pFunc = GetPyFunction(pModule, py_fun_name);
	
	if (pFunc) {
		pResult = __RunPythonFunction(pModule, pFunc, pyInput, num_args, error_code);
		Py_DecRef(pFunc);
	}
	
	return pResult;
}
// calls pFunc and returns result
PyObject* __RunPythonFunction(PyObject *pModule, PyObject *pFunc, PyObject *pyInput[], int num_args, int error_code)
{
	PyObject *pArgs, *pResult;
	
	/* setup Python arguments */
	pArgs = PyTuple_New(num_args);
	for (int ii = 0; ii < num_args; ++ii) {
		if (!pyInput[ii]) {
			Py_DecRef(pArgs);
			printf("Cannot convert argument");
			return NULL;
		}
		int ret = PyTuple_SetItem(pArgs, ii, pyInput[ii]);
	}
	
	/* call Python function */
	pResult = PyObject_CallObject(pFunc, pArgs);
	Py_DecRef(pArgs);
	if (pResult != NULL) {
		return pResult;
	}
	else {
		printf("Call failed");
		return NULL;
	}
}


/**************************************************************************************************
	General purpose string functions
**************************************************************************************************/
// returns the directory given the full path
// must free return value
char* GetDirectoryFromFullPath(const char fp[]) 
{
	const char *dir_end = strrchr(fp, '\\');
	size_t dir_len = strlen(fp) - strlen(dir_end);
	char *dir = (char*)malloc(dir_len+1);
	if (!dir) {
		return NULL;
	}
	strncpy(dir, fp, dir_len);
	
	return dir;
}

// replace all instances of 'rep' with 'with'
// must free return value
char* StrReplace(char *orig, const char *rep, const char *with)
{
	char *result; // the return string
	char *ins;    // the next insert point
	char *tmp;    // varies
	int len_rep;  // length of rep (the string to remove)
	int len_with; // length of with (the string to replace rep with)
	int len_front; // distance between rep and end of last rep
	int count;    // number of replacements
	
	if (!orig || !rep) {
		return NULL;
	}

	len_rep = strlen(rep);
	if (len_rep == 0) {
		return NULL; // empty rep causes infinite loop during count
	}

	if (!with) {
		with = char(0); // replace with nothing, essentially removes all instances of rep
	}
	len_with = strlen(with);
	
	// count the number of replacements needed
	ins = orig;
	for (count = 0; tmp = strstr(ins, rep); ++count) {
		ins = tmp + len_rep;
	}
	
	tmp = result = (char*)malloc( strlen(orig) + (len_with-len_rep)*count + 1 );

	if (!result)
		return NULL;

    // first time through the loop, all the variable are set correctly
    // from here on,
    //    tmp points to the end of the result string
    //    ins points to the next occurrence of rep in orig
    //    orig points to the remainder of orig after "end of rep"
	while (count--) {
		ins = strstr(orig, rep);
		len_front = ins - orig;
		tmp = strncpy(tmp, orig, len_front) + len_front;
		tmp = strcpy(tmp, with) + len_with;
		orig += len_front + len_rep; // move to next "end of rep"
	}
	strcpy(tmp, orig);
	return result;
}


