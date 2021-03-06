/*
 * Jitsi, the OpenSource Java VoIP and Instant Messaging client.
 *
 * Distributable under LGPL license.
 * See terms of license at gnu.org.
 */

#include "MsOutlookUtils.h"
#include "MsOutlookAddrBookContactSourceService.h"

#include "MAPIBitness.h"
#include "MAPISession.h"

#include "com/ComClient.h"
#include "com/MsOutlookAddrBookServer.h"
#include "MsOutlookMAPIHResultException.h"
#include "StringUtils.h"

#include <initguid.h>
#include <jni.h>
#include <Mapidefs.h>
#include <Mapix.h>
#include <windows.h>



HRESULT
MsOutlookUtils_getFolderEntryIDByType
    (LPMDB msgStore,
    ULONG folderEntryIDByteCount, LPENTRYID folderEntryID,
    ULONG *contactsFolderEntryIDByteCount, LPENTRYID *contactsFolderEntryID,
    ULONG flags, ULONG type)
{
    HRESULT hResult;
    ULONG objType;
    LPUNKNOWN folder;

    hResult = msgStore->OpenEntry(
            folderEntryIDByteCount,
            folderEntryID,
            NULL,
            flags,
            &objType,
            &folder);

    if (HR_SUCCEEDED(hResult))
    {
        LPSPropValue prop;

        hResult
            = MsOutlookUtils_HrGetOneProp(
                    (LPMAPIPROP) folder,
                    type,
                    &prop);
        if (HR_SUCCEEDED(hResult))
        {
            LPSBinary bin = &(prop->Value.bin);
            if (S_OK
                    == MAPIAllocateBuffer(
                            bin->cb,
                            (void **) contactsFolderEntryID))
            {
                hResult = S_OK;
                *contactsFolderEntryIDByteCount = bin->cb;
                CopyMemory(*contactsFolderEntryID, bin->lpb, bin->cb);
            }
            else
                hResult = MAPI_E_NOT_ENOUGH_MEMORY;
            MAPIFreeBuffer(prop);
        }
        folder->Release();
    }
    return hResult;
}



/**
 * Get one property for a given contact.
 *
 * @param mapiProp A pointer to the contact.
 * @param propTag The tag of the property to get.
 * @param prop The memory location to store the property value.
 *
 * @return S_OK if everything work fine. Any other value is a failure.
 */
HRESULT
MsOutlookUtils_HrGetOneProp(
        LPMAPIPROP mapiProp,
        ULONG propTag,
        LPSPropValue *prop)
{
    SPropTagArray propTagArray;
    HRESULT hResult;
    ULONG valueCount;
    LPSPropValue values;

    propTagArray.cValues = 1;
    propTagArray.aulPropTag[0] = propTag;

    hResult = mapiProp->GetProps(&propTagArray, 0, &valueCount, &values);
    if (HR_SUCCEEDED(hResult))
    {
        ULONG i;
        jboolean propHasBeenAssignedTo = JNI_FALSE;

        for (i = 0; i < valueCount; i++)
        {
            LPSPropValue value = values;

            values++;
            if (value->ulPropTag == propTag)
            {
                *prop = value;
                propHasBeenAssignedTo = JNI_TRUE;
            }
            else
                MAPIFreeBuffer(value);
        }
        if (!propHasBeenAssignedTo)
            hResult = MAPI_E_NOT_FOUND;
        MAPIFreeBuffer(values);
    }
    return hResult;
}


jobjectArray
MsOutlookUtils_IMAPIProp_GetProps(
        JNIEnv *jniEnv,
        jclass clazz,
        jstring entryId,
        jlongArray propIds,
        jlong flags,
        UUID UUID_Address)
{
    HRESULT hr = E_FAIL;
    jobjectArray javaProps = NULL;
    const char *nativeEntryId = jniEnv->GetStringUTFChars(entryId, NULL);
    jsize propIdCount = jniEnv->GetArrayLength(propIds);
    long nativePropIds[propIdCount];

    for(int i = 0; i < propIdCount; ++i)
    {
        jlong propId;

        jniEnv->GetLongArrayRegion(propIds, i, 1, &propId);
        nativePropIds[i] = propId;
    }

    if(jniEnv->ExceptionCheck())
    {
        jniEnv->ReleaseStringUTFChars(entryId, nativeEntryId);
        return NULL;
    }

    void ** props = NULL;
    unsigned long* propsLength = NULL;
    // b = byteArray, l = long, s = 8 bits string, u = 16 bits string.
    char * propsType = NULL;

    if((props = (void**) malloc(propIdCount * sizeof(void*))) != NULL)
    {
        memset(props, 0, propIdCount * sizeof(void*));
        if((propsLength = (unsigned long*) malloc(
                        propIdCount * sizeof(unsigned long))) != NULL)
        {
            if((propsType = (char*) malloc(propIdCount * sizeof(char)))
                    != NULL)
            {
                IMsOutlookAddrBookServer * iServer = ComClient_getIServer();
                if(iServer)
                {
                    LPWSTR unicodeEntryId
                        = StringUtils::MultiByteToWideChar(nativeEntryId);
                    BSTR comEntryId = SysAllocString(unicodeEntryId);

                    LPSAFEARRAY comPropIds
                        = SafeArrayCreateVector(VT_I4, 0, propIdCount);
                    SafeArrayLock(comPropIds);
                    comPropIds->pvData = nativePropIds;
                    SafeArrayUnlock(comPropIds);

                    LPSAFEARRAY comProps;
                    LPSAFEARRAY comPropsLength;
                    LPSAFEARRAY comPropsType;

                    hr = iServer->IMAPIProp_GetProps(
                            comEntryId,
                            propIdCount,
                            comPropIds,
                            flags,
                            UUID_Address,
                            &comProps,
                            &comPropsLength,
                            &comPropsType);

                    if(HR_SUCCEEDED(hr))
                    {
                        SafeArrayLock(comPropsType);
                        memcpy(
                                propsType,
                                comPropsType->pvData,
                                propIdCount * sizeof(char));
                        SafeArrayUnlock(comPropsType);

                        SafeArrayLock(comPropsLength);
                        memcpy(
                                propsLength,
                                comPropsLength->pvData,
                                propIdCount * sizeof(unsigned long));
                        SafeArrayUnlock(comPropsLength);

                        SafeArrayLock(comProps);
                        byte * data = (byte*) comProps->pvData;
                        for(int j = 0; j < propIdCount; ++j)
                        {
                            if((props[j] = malloc(propsLength[j])) != NULL)
                            {
                                memcpy(props[j], data, propsLength[j]);
                                data += propsLength[j];
                            }
                        }
                        SafeArrayUnlock(comProps);

                        // Decode properties to java
                        jclass objectClass
                            = jniEnv->FindClass("java/lang/Object");
                        if (objectClass)
                        {
                            javaProps = jniEnv->NewObjectArray(
                                    propIdCount,
                                    objectClass,
                                    NULL);
                            for(int j = 0; j < propIdCount; ++j)
                            {
                                // byte array
                                if(propsType[j] == 'b' && props[j] != NULL)
                                {
                                    jbyteArray value = jniEnv->NewByteArray(
                                                (jsize) propsLength[j]);
                                    if(value)
                                    {
                                        jbyte *bytes
                                            = jniEnv->GetByteArrayElements(
                                                    value, NULL);

                                        if (bytes)
                                        {
                                            memcpy(
                                                    bytes,
                                                    props[j],
                                                    propsLength[j]);
                                            jniEnv->ReleaseByteArrayElements(
                                                    value,
                                                    bytes,
                                                    0);
                                            jniEnv->SetObjectArrayElement(
                                                    javaProps,
                                                    j,
                                                    value);
                                        }
                                    }
                                }
                                // long
                                else if(propsType[j] == 'l' && props[j] != NULL)
                                {
                                    jclass longClass
                                        = jniEnv->FindClass("java/lang/Long");
                                    if (longClass)
                                    {
                                        jmethodID longMethodID
                                            = jniEnv->GetMethodID(
                                                longClass,
                                                "<init>",
                                                "(J)V");

                                        if (longMethodID)
                                        {
                                            jlong l = (jlong)(*((long*)props[j]));
                                            memcpy(&l, props[j], propsLength[j]);
                                            jobject value = jniEnv->NewObject(
                                                    longClass,
                                                    longMethodID,
                                                    l);

                                            if (value)
                                            {
                                                jniEnv->SetObjectArrayElement(
                                                        javaProps,
                                                        j,
                                                        value);
                                            }
                                        }
                                    }
                                }
                                // 8 bits string
                                else if(propsType[j] == 's' && props[j] != NULL)
                                {
                                    jstring value = jniEnv->NewStringUTF(
                                            (const char*) props[j]);
                                    if (value)
                                    {
                                        jniEnv->SetObjectArrayElement(
                                                javaProps,
                                                j,
                                                value);
                                    }
                                }
                                // 16 bits string
                                else if(propsType[j] == 'u' && props[j] != NULL)
                                {
                                    jstring value
                                        = jniEnv->NewString(
                                            (const jchar *) props[j],
                                            wcslen((const wchar_t *) props[j]));
                                    if (value)
                                    {
                                        jniEnv->SetObjectArrayElement(
                                                javaProps,
                                                j,
                                                value);
                                    }
                                }
                                else if(propsType[j] == 'B' && props[j] != NULL)
                                {
                                	jclass booleanClass
                                		= jniEnv->FindClass("java/lang/Boolean");
                                	jmethodID boolMethodID
                                		= jniEnv->GetStaticMethodID(
                                				booleanClass,
                                				"valueOf",
                                				"(Z)Ljava/lang/Boolean;");
                                	bool value = false;
                                	if((bool)props[j])
                                		value = true;
                                	jobject jValue
                                		= jniEnv->CallStaticObjectMethod(
                                				booleanClass,
                                				boolMethodID,
                                				value);
									jniEnv->SetObjectArrayElement(
											javaProps,
											j,
											jValue);
                                }
                                else if(propsType[j] == 't' && props[j] != NULL)
                                {	char dateTime[20];
                                	LPSYSTEMTIME sysTime
                                		= (LPSYSTEMTIME) props[j];
                                	sprintf(dateTime,
                                			"%u-%02u-%02u %02u:%02u:%02u",
                                			sysTime->wYear, sysTime->wMonth,
                                			sysTime->wDay, sysTime->wHour,
                                			sysTime->wMinute, sysTime->wSecond);
                                	jstring value = jniEnv->NewStringUTF(
											(const char*) dateTime);
                                	if (value)
									{
										jniEnv->SetObjectArrayElement(
												javaProps,
												j,
												value);
									}
                                }

                                if(jniEnv->ExceptionCheck())
                                    javaProps = NULL;
                            }
                        }
                    }
                    else
                    {
                        MsOutlookMAPIHResultException_throwNew(
                                jniEnv,
                                hr,
                                __FILE__, __LINE__);
                    }

                    SafeArrayDestroy(comPropsType);
                    SafeArrayDestroy(comPropsLength);
                    SafeArrayDestroy(comProps);
                    SafeArrayDestroy(comPropIds);
                    SysFreeString(comEntryId);
                    free(unicodeEntryId);
                }


                for(int j = 0; j < propIdCount; ++j)
                {
                    if(props[j] != NULL)
                        free(props[j]);
                }
                free(propsType);
            }
            free(propsLength);
        }
        free(props);
    }

    jniEnv->ReleaseStringUTFChars(entryId, nativeEntryId);

    return javaProps;
}
