#include <stdlib.h>
#include "libcommon.h"
#include "jniutil.h"
#include "string.h"
#include "utils/Log.h"

#ifdef __cplusplus
extern "C" {
#endif

/* JNI utility functions. */
/* Get class field id, return NULL if fail. */
jfieldID getFieldID(JNIEnv *env, jobject obj, const char *field_name, const char *type) {
    jclass clazz = NULL;
    jfieldID fid = NULL;

	if (NULL == env || NULL == obj)
		return NULL;
    clazz = (*env)->GetObjectClass(env, obj);
    if (NULL == clazz)
        return NULL;
	fid = (*env)->GetFieldID(env, clazz, field_name, type);
	if (NULL == fid)
		return NULL;

	return fid;
}

/* -- Setter function -- */
/* Set the boolean field value of an java object, return 1 if success, otherwise -1. */
int setBooleanField(JNIEnv *env, jobject obj, const char *field_name, jboolean value) {
	jfieldID fid = NULL;

	if (NULL == (fid = getFieldID(env, obj, field_name, SIGNATURE_BOOLEAN)))
		return -1;
	(*env)->SetBooleanField(env, obj, fid, value);
    return 1;
}

/* Set the byte field value of an java object, return 1 if success, otherwise -1. */
int setByteField(JNIEnv *env, jobject obj, const char *field_name, jbyte value) {
	jfieldID fid = NULL;

	if (NULL == (fid = getFieldID(env, obj, field_name, SIGNATURE_BYTE)))
		return -1;
	(*env)->SetByteField(env, obj, fid, value);
    return 1;
}

/* Set the short field value of an java object, return 1 if success, otherwise -1. */
int setShortField(JNIEnv *env, jobject obj, const char *field_name, jshort value) {
	jfieldID fid = NULL;

	if (NULL == (fid = getFieldID(env, obj, field_name, SIGNATURE_SHORT)))
		return -1;
	(*env)->SetShortField(env, obj, fid, value);
    return 1;
}

/* Set the integer field value of an java object, return 1 if success, otherwise -1. */
int setIntField(JNIEnv *env, jobject obj, const char *field_name, jint value) {
	jfieldID fid = NULL;

	if (NULL == (fid = getFieldID(env, obj, field_name, SIGNATURE_INT)))
		return -1;
	(*env)->SetIntField(env, obj, fid, value);
    return 1;
}

/* Set the long field value of an java object, return 1 if success, otherwise -1. */
int setLongField(JNIEnv *env, jobject obj, const char *field_name, jlong value) {
	jfieldID fid = NULL;

	if (NULL == (fid = getFieldID(env, obj, field_name, SIGNATURE_LONG)))
		return -1;
	(*env)->SetLongField(env, obj, fid, value);
	return 1;
}

/* Set the float field value of an java object, return 1 if success, otherwise -1. */
int setFloatField(JNIEnv *env, jobject obj, const char *field_name, jfloat value) {
	jfieldID fid = NULL;

	if (NULL == (fid = getFieldID(env, obj, field_name, SIGNATURE_FLOAT)))
		return -1;
	(*env)->SetFloatField(env, obj, fid, value);
	return 1;
}

/* Set the double field value of an java object, return 1 if success, otherwise -1. */
int setDoubleField(JNIEnv *env, jobject obj, const char *field_name, jdouble value) {
	jfieldID fid = NULL;

	if (NULL == (fid = getFieldID(env, obj, field_name, SIGNATURE_DOUBLE)))
		return -1;
	(*env)->SetDoubleField(env, obj, fid, value);
	return 1;
}

/* Set the string field value of an java object, return 1 if success, otherwise -1. */
int setStringField(JNIEnv *env, jobject obj, const char *field_name, const char *cstr) {
	jfieldID fid = NULL;
	jstring jstr = NULL;

	fid = getFieldID(env, obj, field_name, SIGNATURE_STRING);
	if (NULL == fid)
		return -1;

	jstr = (*env)->NewStringUTF(env, cstr);
	(*env)->SetObjectField(env, obj, fid, jstr);
	return 1;
}


/* -- Getter function -- */
/* Get the boolean field value of an java object, return 1 if success, otherwise -1. */
int getBooleanField(JNIEnv *env, jobject obj, const char *field_name, jboolean *value) {
	jfieldID fid = NULL;

	if (NULL == (fid = getFieldID(env, obj, field_name, SIGNATURE_BOOLEAN)))
		return -1;
	*value = (*env)->GetBooleanField(env, obj, fid);
    return 1;
}

/* Get the byte field value of an java object, return 1 if success, otherwise -1. */
int getByteField(JNIEnv *env, jobject obj, const char *field_name, jbyte *value) {
	jfieldID fid = NULL;

	if (NULL == (fid = getFieldID(env, obj, field_name, SIGNATURE_BYTE)))
		return -1;
	*value = (*env)->GetByteField(env, obj, fid);
    return 1;
}

/* Get the short field value of an java object, return 1 if success, otherwise -1. */
int getShortField(JNIEnv *env, jobject obj, const char *field_name, jshort *value) {
	jfieldID fid = NULL;

	if (NULL == (fid = getFieldID(env, obj, field_name, SIGNATURE_SHORT)))
		return -1;
	*value = (*env)->GetShortField(env, obj, fid);
    return 1;
}

/* Get the int field value of an java object, return 1 if success, otherwise -1. */
int getIntField(JNIEnv *env, jobject obj, const char *field_name, jint *value) {
	jfieldID fid = NULL;

	if (NULL == (fid = getFieldID(env, obj, field_name, SIGNATURE_INT)))
		return -1;
	*value = (*env)->GetIntField(env, obj, fid);
	return 1;
}

/* Get the long field value of an java object, return 1 if success, otherwise -1. */
int getLongField(JNIEnv *env, jobject obj, const char *field_name, jlong *value) {
	jfieldID fid = NULL;

	if (NULL == (fid = getFieldID(env, obj, field_name, SIGNATURE_LONG)))
		return -1;
	*value = (*env)->GetLongField(env, obj, fid);
	return 1;
}

/* Get the float field value of an java object, return 1 if success, otherwise -1. */
int getFloatField(JNIEnv *env, jobject obj, const char *field_name, jfloat *value) {
	jfieldID fid = NULL;

	if (NULL == (fid = getFieldID(env, obj, field_name, SIGNATURE_FLOAT)))
		return -1;
	*value = (*env)->GetFloatField(env, obj, fid);
	return 1;
}

/* Get the double field value of an java object, return 1 if success, otherwise -1. */
int getDoubleField(JNIEnv *env, jobject obj, const char *field_name, jdouble *value) {
	jfieldID fid = NULL;

	if (NULL == (fid = getFieldID(env, obj, field_name, SIGNATURE_DOUBLE)))
		return -1;
	*value = (*env)->GetDoubleField(env, obj, fid);
	return 1;
}

/* Get the string field value of an java object, return 1 if success, otherwise -1. */
int getStringField(JNIEnv *env, jobject obj, const char *field_name, char *cstr, int max_len) {
	jfieldID fid = NULL;
	jstring jstr = NULL;
	char *str_buf = NULL;
	int length = 0;

	fid = getFieldID(env, obj, field_name, SIGNATURE_STRING);
	if (NULL == fid)
		return -1;

	jstr = (*env)->GetObjectField(env, obj, fid);
	str_buf = (char *) (*env)->GetStringUTFChars(env, jstr, 0);
	length = (*env)->GetStringUTFLength(env, jstr);
	length = (length >= max_len) ? (max_len - 1) : length;
	memset(cstr, 0, max_len);
	memcpy(cstr, str_buf, length);
	(*env)->ReleaseStringUTFChars(env, jstr, str_buf);

	return 1;
}

JavaVM *jnienv_to_javavm(JNIEnv *env)
{
    JavaVM *vm;
    return (*env)->GetJavaVM(env, &vm) >= 0 ? vm : NULL;
}

JNIEnv *javavm_to_jnienv(JavaVM *vm)
{
    JNIEnv *env;
    return (*vm)->GetEnv(vm, (void **) & env, JNI_VERSION_1_4) >= 0 ? env : NULL;
}

static jstring _cs2js (JNIEnv *env, const char *cstr, const char *encoding, jint length)
{
	/*
	 * get String class and init method
	 */
	jclass string_class = (*env)->FindClass (env, "java/lang/String");
	jmethodID string_init_method = (*env)->GetMethodID (env, string_class, "<init>", "([BLjava/lang/String;)V");

	/*
	 * new byte []
	 */
	jbyteArray byte_array = (*env)->NewByteArray (env, length);
	(*env)->SetByteArrayRegion (env, byte_array, 0, length, (jbyte *) cstr);

	/*
	 * return the String object, public String (byte [] data, String charsetName)
	 */
	return (jstring) (*env)->NewObject (env, string_class, string_init_method, byte_array, (jstring) (*env)->NewStringUTF (env, encoding));
}

jstring cs2jstring (JNIEnv *env, const char *cstr)
{
	return _cs2js (env, cstr, "utf-8", strlen (cstr));
}

jstring cs2jstringraw (JNIEnv *env, const char *cstr, jint length)
{
	return _cs2js (env, cstr, "iso-8859-1", length);
}

static char *_js2cs (JNIEnv *env, jstring jstr, const char *encoding)
{
	char *cstr = NULL;

	/*
	 * get String class and getBytes method
	 */
	jclass string_class = (*env)->FindClass (env, "java/lang/String");
	jmethodID string_getbytes_method = (*env)->GetMethodID (env, string_class, "getBytes", "(Ljava/lang/String;)[B");

	/*
	 * new byte [] and its memory address, public byte [] getBytes (String charsetName)
	 */
	jbyteArray byte_array = (jbyteArray) (*env)->CallObjectMethod (env, jstr, string_getbytes_method, (jstring) (*env)->NewStringUTF (env, encoding));
	jbyte *byte_array_address = (*env)->GetByteArrayElements (env, byte_array, NULL);
	jsize length = (*env)->GetArrayLength (env, byte_array);

	if (byte_array_address && (length > 0))
	{
		cstr = (char *) malloc (length + 1 /* null terminated */);

		if (cstr)
		{
			memcpy (cstr, byte_array_address, length);
			cstr [length] = 0;
		}
	}

	/*
	 * release the byte []
	 */
	(*env)->ReleaseByteArrayElements (env, byte_array, byte_array_address, 0);
	return cstr;
}

char *jstring2cs (JNIEnv *env, jstring jstr)
{
	return _js2cs (env, jstr, "utf-8");
}

char *jstring2csraw (JNIEnv *env, jstring jstr)
{
	return _js2cs (env, jstr, "iso-8859-1");
}

#if 0
/*
 * Register several native methods for one class.
 */
int registerNativeMethods(JNIEnv *env, const char *className, JNINativeMethod *gMethods, int numMethods)
{
    jclass clazz = (*env)->FindClass(env, className);
    if (clazz == NULL)
    {
        fLOGW("Native registration unable to find class '%s'\n", className);
        return JNI_FALSE;
    }
    if ((*env)->RegisterNatives(env, clazz, gMethods, numMethods) < 0)
    {
        fLOGW("RegisterNatives failed for '%s'\n", className);
        return JNI_FALSE;
    }
    return JNI_TRUE;
}

/*
 * Register native methods for all classes we know about.
 */
static int registerNatives (JNIEnv *env)
{
    if (! registerNativeMethods (env, LIB_CLASS_NAME, gMethods, sizeof (gMethods) / sizeof (gMethods [0])))
        return JNI_FALSE;
    return JNI_TRUE;
}
#endif
#ifdef __cplusplus
}
#endif
