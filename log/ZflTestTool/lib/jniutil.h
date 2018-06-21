
#ifndef _HTCSSDTESTFAC_HTCJNIUTIL_H
#define _HTCSSDTESTFAC_HTCJNIUTIL_H

#include <jni.h>
#include "JNIHelp.h"

#define SIGNATURE_BOOLEAN	("Z")
#define SIGNATURE_BYTE		("B")
#define SIGNATURE_CHAR		("C")
#define SIGNATURE_SHORT		("S")
#define SIGNATURE_INT		("I")
#define SIGNATURE_LONG		("J")
#define SIGNATURE_FLOAT		("F")
#define SIGNATURE_DOUBLE	("D")
#define SIGNATURE_STRING	("Ljava/lang/String;")

#ifdef __cplusplus
extern "C" {
#endif

/* -- Setter functions -- */
/* Set the boolean field value of an java object, return 1 if success, otherwise -1. */
int setBooleanField(JNIEnv *env, jobject obj, const char *field_name, jboolean value);
/* Set the byte field value of an java object, return 1 if success, otherwise -1. */
int setByteField(JNIEnv *env, jobject obj, const char *field_name, jbyte value);
/* Set the short field value of an java object, return 1 if success, otherwise -1. */
int setShortField(JNIEnv *env, jobject obj, const char *field_name, jshort value);
/* Set the integer field value of an java object, return 1 if success, otherwise -1. */
int setIntField(JNIEnv *env, jobject obj, const char *field_name, jint value);
/* Set the long field value of an java object, return 1 if success, otherwise -1. */
int setLongField(JNIEnv *env, jobject obj, const char *field_name, jlong value);
/* Set the float field value of an java object, return 1 if success, otherwise -1. */
int setFloatField(JNIEnv *env, jobject obj, const char *field_name, jfloat value);
/* Set the double field value of an java object, return 1 if success, otherwise -1. */
int setDoubleField(JNIEnv *env, jobject obj, const char *field_name, jdouble value);
/* Set the string field value of an java object, return 1 if success, otherwise -1. */
int setStringField(JNIEnv *env, jobject obj, const char *field_name, const char *cstr);

/* -- Getter functions -- */
/* Get the boolean field value of an java object, return 1 if success, otherwise -1. */
int getBooleanField(JNIEnv *env, jobject obj, const char *field_name, jboolean *value);
/* Get the byte field value of an java object, return 1 if success, otherwise -1. */
int getByteField(JNIEnv *env, jobject obj, const char *field_name, jbyte *value);
/* Get the short field value of an java object, return 1 if success, otherwise -1. */
int getShortField(JNIEnv *env, jobject obj, const char *field_name, jshort *value);
/* Get the int field value of an java object, return 1 if success, otherwise -1. */
int getIntField(JNIEnv *env, jobject obj, const char *field_name, jint *value);
/* Get the long field value of an java object, return 1 if success, otherwise -1. */
int getLongField(JNIEnv *env, jobject obj, const char *field_name, jlong *value);
/* Get the float field value of an java object, return 1 if success, otherwise -1. */
int getFloatField(JNIEnv *env, jobject obj, const char *field_name, jfloat *value);
/* Get the double field value of an java object, return 1 if success, otherwise -1. */
int getDoubleField(JNIEnv *env, jobject obj, const char *field_name, jdouble *value);
/* Get the string field value of an java object, return 1 if success, otherwise -1. */
int getStringField(JNIEnv *env, jobject obj, const char *field_name, char *cstr, int max_len);


/* String conversion. */
jstring cs2jstring (JNIEnv *env, const char *cstr);
jstring cs2jstringraw (JNIEnv *env, const char *cstr, jint length);
char *jstring2cs (JNIEnv *env, jstring jstr);
char *jstring2csraw (JNIEnv *env, jstring jstr);

/* JVM and environment converstion. */
JavaVM *jnienv_to_javavm (JNIEnv *env);
JNIEnv *javavm_to_jnienv (JavaVM *vm);
int registerNativeMethods(JNIEnv *env, const char *className, JNINativeMethod *gMethods, int numMethods);

#ifdef __cplusplus
}
#endif

#endif /* _HTCSSDTESTFAC_HTCJNIUTIL_H */
