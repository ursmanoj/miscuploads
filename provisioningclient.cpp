/* *****************************************************************
 *
 * Copyright 2015 Samsung Electronics All Rights Reserved.
 *
 *
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * *****************************************************************/

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <string>
#include <map>
#include <cstdlib>
#include <pthread.h>
#include <mutex>
#include <condition_variable>

#include "logger.h"
#include "oic_malloc.h"
#include "oic_string.h"
#include "OCPlatform.h"
#include "OCApi.h"
#include "OCProvisioningManager.hpp"
#include "oxmjustworks.h"
#include "oxmrandompin.h"
#include "aclresource.h"
#include "coap/utlist.h"

#define MAX_PERMISSION_LENGTH (5)
#define ACL_RESRC_ARRAY_SIZE (3)
#define CREATE (1)
#define READ (2)
#define UPDATE (4)
#define DELETE (8)
#define NOTIFY (16)
#define DASH '-'
#define PREDEFINED_TIMEOUT (10)
#define MAX_OWNED_DEVICE (10)
#define TAG  "provisioningclient"

#define JSON_DB_PATH "./oic_svr_db_client.json"
#define DAT_DB_PATH "./oic_svr_db_client.dat"
#define DEV_STATUS_ON "DEV_STATUS_ON"
#define DEV_STATUS_OFF "DEV_STATUS_OFF"

#define DISCOVERY_TIMEOUT 5

static const OicSecPrm_t  SUPPORTED_PRMS[1] =
{
    PRM_PRE_CONFIGURED,
};

using namespace OC;

DeviceList_t pUnownedDevList, pOwnedDevList, pMOTEnabledDeviceList;
static int transferDevIdx, ask = 1;
static OicSecPconf_t g_pconf;
static uint16_t g_credId = 0;

static FILE* client_open(const char *path, const char *mode)
{
    if (0 == strcmp(path, OC_SECURITY_DB_DAT_FILE_NAME))
    {
        return fopen(DAT_DB_PATH, mode);
    }
    else
    {
        return fopen(path, mode);
    }
}

void printMenu()
{
    std::cout << "\nChoose an option:"<<std::endl;
    std::cout << "   1. UnOwned Device discovery"<<std::endl;
    std::cout << "   2. Owned Device discovery"<<std::endl;
    std::cout << "   3. Ownership transfer"<<std::endl;
    std::cout << "   4. Provision ACL"<<std::endl;
    std::cout << "   5. Provision Credentials"<<std::endl;
    std::cout << "   6. Credential & ACL provisioning b/w two devices"<<std::endl;
    std::cout << "   7. Unlink Devices"<<std::endl;
    std::cout << "   8. Remove Device"<<std::endl;
    std::cout << "   9. Remove Device using UUID"<<std::endl;
    std::cout << "  10. Get Linked Devices"<<std::endl;
    std::cout << "  11. Get Device Status"<<std::endl;
    std::cout << "  12. Provision Direct-Pairing Configuration"<<std::endl;
#if defined(__WITH_DTLS__) || defined(__WITH_TLS__)
    std::cout << "  13. Save the Trust Cert. Chain into Cred of SVR"<<std::endl;
    std::cout << "  14. Provision the Trust Cert. Chain"<<std::endl;
    std::cout << "  15. Read trust cert chain"<<std::endl;
#endif // __WITH_DTLS__ || __WITH_TLS__
#ifdef MULTIPLE_OWNER
    std::cout << "  16. Change Multiple Ownership Transfer Mode"<<std::endl;
    std::cout << "  17. Select OxM method for Multiple Ownership Transfer"<<std::endl;
    std::cout << "  18. Multiple Ownership Transfer Enabled Devices Discovery"<<std::endl;
    std::cout << "  19. Provision pre configure PIN for Multiple Ownership Transfer Mode"<<std::endl;
    std::cout << "  20. Add pre configure PIN for Multiple Ownership Transfer Mode"<<std::endl;
#endif
    std::cout << "  21. Configure SVRdb as Self-OwnerShip"<<std::endl;
	std::cout << "  30. Discover LED resources"<<std::endl;
	std::cout << "  31. Discover LIGHT resources"<<std::endl;

    std::cout << "  99. Exit loop"<<std::endl;
}

void moveTransferredDevice()
{
    pOwnedDevList.push_back(pUnownedDevList[transferDevIdx]);
    pUnownedDevList.erase(pUnownedDevList.begin() + transferDevIdx);
}

void OnInputPinCB(OicUuid_t deviceId, char* pinBuf, size_t bufSize)
{
    OC_UNUSED(deviceId);
    if(pinBuf)
    {
        std::cout <<"INPUT PIN : ";
        std::string ptr;
        std::cin >> ptr;
        OICStrcpy(pinBuf, bufSize, ptr.c_str());
        return;
    }
}

void printUuid(OicUuid_t uuid)
{
    for (int i = 0; i < UUID_LENGTH; i++)
    {
        std::cout <<std::hex << uuid.id[i] << " ";
    }
    std::cout<<std::endl;
}

void ownershipTransferCB(PMResultList_t *result, int hasError)
{
    if (hasError)
    {
        std::cout << "Error!!! in OwnershipTransfer"<<std::endl;
    }
    else
    {
        std::cout<< "\nTransferred Ownership successfuly for device : ";
        printUuid(result->at(0).deviceId);
        delete result;

        moveTransferredDevice();
    }
    ask = 1;
}

void printStatus(int status)
{
    static std::map<int, std::string> devStatus = {{1<<0, DEV_STATUS_ON}, {1<<1, DEV_STATUS_OFF}};

    std::cout <<devStatus[status] <<std::endl;
}

void printDevices(DeviceList_t &list)
{
   for (unsigned int i = 0; i < list.size(); i++ )
   {
      std::cout << "Device "<< i+1 <<" ID: ";
      std::cout << list[i]->getDeviceID() << " From IP: ";
      std::cout << list[i]->getDevAddr() <<" Port: "<< list[i]->getDevPtr()->securePort<< std::endl;
   }
}

/**
 * Callback function for provisioning ACL, Credentials.
 *
 * @param[in]    result Result list
 * @param[in] hasError indicates if the result has error
 */
void provisionCB(PMResultList_t *result, int hasError)
{
   if (hasError)
   {
       std::cout << "Error in provisioning operation!"<<std::endl;
   }
   else
   {
       std::cout<< "\nReceived provisioning results: ";
       for (unsigned int i = 0; i < result->size(); i++)
       {
           std::cout << "Result is = " << result->at(i).res <<" for device ";
           printUuid(result->at(i).deviceId);
       }

       delete result;
   }
   printMenu();
   ask = 1;
}
/**
 *
 * Ask user with which devices it wants to make further actions.
 * All possible error checks included.
 * Default behavior in case if only one options leaves are included too.
 * Expect that user count devices from 1, f.e. 1st, 2nd, 3rd, etc
 * Use DeviceList_t instead of devicesCount because of print devices info
 *
 * @param[in] list owned devices list.
 * @param[out] out device number array.
 * @param[in] count how many device numbers need to read.
 * @return 0 in case of success and other value otherwise.
 */
int readDeviceNumber(DeviceList_t &list, int count, int *out)
{
   if (out == NULL || count <= 0)
   {
      std::cout << "Error! Please put valid input parameters" << std::endl;
      return -1;
   }

   int devicesCount = list.size();

   //Print current list of owned devices
   std::cout <<"Owned devices, count = " << devicesCount << std::endl;
   printDevices(list);

   if (devicesCount < count)
   {
      std::cout << "You can't proceed with selected action because Owned devices count ( ";
      std::cout << devicesCount << " ) are less then required devices ( " << count << " ).";
      std::cout << "You may need to discover devices again" << std::endl;
      return -2;
   }

   std::cout << "Select " << count << " device(s) for provisioning" << std::endl;

   for (int curr = 0; curr < count; curr++)
   {
      //select last device by default if only 1 option exist
      //from user's point of view device counting starts from 1,
      //so 1st, 2nd, 3rd, etc devices
      if ((curr == count - 1) && (devicesCount == count))
      {
         int sum = 0;
         for (int i = 0; i < curr; i++)
         {
             sum += out[i];
         }

         out[curr] = (count*(count+1))/2 - sum;

         std::cout << "Device " << curr + 1 << " : " << out[curr];
         std::cout << " - selected automatically (because no other options exist)" << std::endl;
         break;
      }

      int choice;
      std::cout << "Device " << curr + 1 << " : ";
      std::cin >> choice;

      if (choice < 1 || choice > devicesCount)
      {
         std::cout << "Error! You should enter valid device number!" << std::endl;
         return -3;
      }

      //check that user doesn't select the same device twice
      for (int i = 0; i < curr; i++)
      {
         if (out[i] == choice)
         {
            std::cout << "Error! You cannot select the same device twice!" << std::endl;
            return -4;
         }
      }

      out[curr] = choice;
   }

   //Users count devices from 1, so 1st, 2nd, 3rd, etc device
   //But deviceList array start index is 0, so need to decrease all numbers to 1
   for (int i = 0; i < count; i++) out[i] -= 1;

   return 0;
}

/**
 * Perform cleanup for ACL
 * @param[in]    ACL
 */
static void deleteACL(OicSecAcl_t *acl)
{
    DeleteACLList(acl);

}

/**
 * Calculate ACL permission from string to bit
 *
 * @param[in] temp_psm    Input data of ACL permission string
 * @param[in,out] pms    The pointer of ACL permission value
 * @return  0 on success otherwise -1.
 */
static int CalculateAclPermission(const char *temp_pms, uint16_t *pms)
{
    int i = 0;

    if (NULL == temp_pms || NULL == pms)
    {
        return -1;
    }
    *pms = 0;
    while (temp_pms[i] != '\0')
    {
        switch (temp_pms[i])
        {
            case 'C':
                {
                    *pms += CREATE;
                    i++;
                    break;
                }
            case 'R':
                {
                    *pms += READ;
                    i++;
                    break;
                }
            case 'U':
                {
                    *pms += UPDATE;
                    i++;
                    break;
                }
            case 'D':
                {
                    *pms += DELETE;
                    i++;
                    break;
                }
            case 'N':
                {
                    *pms += NOTIFY;
                    i++;
                    break;
                }
            case '_':
                {
                    i++;
                    break;
                }
            default:
                {
                    return -1;
                }
        }
    }
    return 0;
}

/**
 * Get the ACL property from user
 *
 * @param[in]    ACL Datastructure to save user inputs
 * @return  0 on success otherwise -1.
 */
static int InputACL(OicSecAcl_t *acl)
{
    int ret;
    char *temp_id, *temp_rsc, *temp_pms;

    printf("******************************************************************************\n");
    printf("-Set ACL policy for target device\n");
    printf("******************************************************************************\n");
    //Set Subject.
    printf("-URN identifying the subject\n");
    printf("ex) 1111-1111-1111-1111 (16 Numbers except to '-')\n");
    printf("Subject : ");
    ret = scanf("%19ms", &temp_id);

    OicSecAce_t* ace = (OicSecAce_t*)OICCalloc(1, sizeof(OicSecAce_t));
    if(NULL == ace)
    {
        OIC_LOG(ERROR, TAG, "Error while memory allocation");
        return -1;
    }
    LL_APPEND(acl->aces, ace);

    if (1 == ret)
    {
        for (int i = 0, j = 0; temp_id[i] != '\0'; i++)
        {
            if (DASH != temp_id[i])
                ace->subjectuuid.id[j++] = temp_id[i];
        }
        OICFree(temp_id);
    }
    else
    {
        deleteACL(acl);
        printf("Error while input\n");
        return -1;
    }

    //Set Resource.
    size_t resourcesLen = 0;
    printf("Num. of Resource : ");
    ret = scanf("%" PRIuPTR, &resourcesLen);
    if ((1 != ret) || (resourcesLen <= 0 || resourcesLen > 50))
    {
        deleteACL(acl);
        printf("Error while input\n");
        return -1;
    }
    printf("-URI of resource\n");
    printf("ex)/oic/sh/temp/0 (Max_URI_Length: %d Byte )\n", MAX_URI_LENGTH);
    for(size_t i = 0; i < resourcesLen; i++)
    {
        OicSecRsrc_t* rsrc = (OicSecRsrc_t*)OICCalloc(1, sizeof(OicSecRsrc_t));
        if(NULL == rsrc)
        {
            deleteACL(acl);
            OIC_LOG(ERROR, TAG, "Error while memory allocation");
            return -1;
        }

        LL_APPEND(ace->resources, rsrc);

        printf("[%" PRIuPTR "]Resource : ", i + 1);
        ret = scanf("%64ms", &temp_rsc);
        if (1 != ret)
        {
            printf("Error while input\n");
            return -1;
        }

        rsrc->href = OICStrdup(temp_rsc);
        OICFree(temp_rsc);

        char* rsrc_in = NULL;
        size_t arrLen = 0;
        while(1)
        {
            printf("         Enter Number of resource type for [%s]: ", rsrc->href);
            for(int ret=0; 1!=ret; )
            {
                ret = scanf("%" SCNuPTR, &arrLen);
                for( ; 0x20<=getchar(); );  // for removing overflow garbages
                                            // '0x20<=code' is character region
            }
            if(ACL_RESRC_ARRAY_SIZE >= arrLen)
            {
                break;
            }
            printf("     Entered Wrong Number. Please Enter under %d Again\n", ACL_RESRC_ARRAY_SIZE);
        }

        rsrc->typeLen = arrLen;
        rsrc->types = (char**)OICCalloc(arrLen, sizeof(char*));
        if(!rsrc->types)
        {
            OIC_LOG(ERROR, TAG, "createAcl: OICCalloc error return");
            goto error;
        }

        for(size_t i = 0; i < arrLen; i++)
        {
            printf("         Enter ResourceType[%" PRIuPTR "] Name (e.g. core.led): ", i+1);
            for(int ret=0; 1!=ret; )
            {
                ret = scanf("%64ms", &rsrc_in);  // '128' is ACL_RESRC_MAX_LEN
                for( ; 0x20<=getchar(); );  // for removing overflow garbages
                                            // '0x20<=code' is character region
            }
            rsrc->types[i] = OICStrdup(rsrc_in);
            OICFree(rsrc_in);
            if(!rsrc->types[i])
            {
                OIC_LOG(ERROR, TAG, "createAcl: OICStrdup error return");
                goto error;
            }
        }

        while(1)
        {
            printf("         Enter Number of interface name for [%s]: ", rsrc->href);
            for(int ret=0; 1!=ret; )
            {
                ret = scanf("%" SCNuPTR, &arrLen);
                for( ; 0x20<=getchar(); );  // for removing overflow garbages
                                            // '0x20<=code' is character region
            }
            if(ACL_RESRC_ARRAY_SIZE >= arrLen)
            {
                break;
            }
            printf("     Entered Wrong Number. Please Enter under %d Again\n", ACL_RESRC_ARRAY_SIZE);
        }

        rsrc->interfaceLen = arrLen;
        rsrc->interfaces = (char**)OICCalloc(arrLen, sizeof(char*));
        if(!rsrc->interfaces)
        {
            OIC_LOG(ERROR, TAG, "createAcl: OICCalloc error return");
            goto error;
        }

        for(size_t i = 0; i < arrLen; i++)
        {
            printf("         Enter interfnace[%" PRIuPTR "] Name (e.g. oic.if.baseline): ", i+1);
            for(int ret=0; 1!=ret; )
            {
                ret = scanf("%64ms", &rsrc_in);  // '128' is ACL_RESRC_MAX_LEN
                for( ; 0x20<=getchar(); );  // for removing overflow garbages
                                            // '0x20<=code' is character region
            }
            rsrc->interfaces[i] = OICStrdup(rsrc_in);
            OICFree(rsrc_in);
            if(!rsrc->interfaces[i])
            {
                OIC_LOG(ERROR, TAG, "createAcl: OICStrdup error return");
                goto error;
            }
        }

    }

    // Set Permission
    do
    {
        printf("-Set the permission(C,R,U,D,N)\n");
        printf("ex) CRUDN, CRU_N,..(5 Charaters)\n");
        printf("Permission : ");
        ret = scanf("%5ms", &temp_pms);
        if (1 != ret)
        {
            printf("Error while input\n");
            goto error;
        }
        ret = CalculateAclPermission(temp_pms, &(ace->permission));
        OICFree(temp_pms);
    } while (0 != ret );

    // Set Rowner
    printf("-URN identifying the rowner\n");
    printf("ex) 1111-1111-1111-1111 (16 Numbers except to '-')\n");

    printf("Rowner : ");
    ret = scanf("%19ms", &temp_id);
    if (1 != ret)
    {
        printf("Error while input\n");
        goto error;
    }

    for (int k = 0, j = 0; temp_id[k] != '\0'; k++)
    {
        if (DASH != temp_id[k])
        {
            acl->rownerID.id[j++] = temp_id[k];
        }
    }
    OICFree(temp_id);

    return 0;

error:
    DeleteACLList(acl);
    return -1;
}

static int InputCredentials(Credential &cred)
{
   int choice;

   do
   {
       std::cout << "Select credential type from following values:" << std::endl;
       std::cout << "1:  symmetric pair-wise key" << std::endl;
       std::cout << "2:  symmetric group key" << std::endl;
       std::cout << "4:  asymmetric key" << std::endl;
       std::cout << "8:  signed asymmetric key (aka certificate)" << std::endl;
       std::cout << "16: PIN /password" << std::endl;
       std::cout << "Your choice: ";

       std::cin >> choice;

       switch (choice){
           case 1:
               cred.setCredentialType(static_cast<OicSecCredType_t>(choice));
               choice = 0; //validation of the accepted choice.
               break;
           case 2:
           case 4:
           case 8:
           case 16:
               std::cout << "selected type is not supported yet" << std::endl;
               break;
           default:
               std::cout << "Error! Please select valid credential type" << std::endl;
               break;
       }
   } while(0 != choice);

   std::cout << "Please enter key size (valid size is 128 or 256) :";
   std::cin >> choice;

   if(128 == choice)
   {
       cred.setCredentialKeySize(OWNER_PSK_LENGTH_128);
   }
   else if(256 == choice)
   {
        cred.setCredentialKeySize(OWNER_PSK_LENGTH_256);
   }
   else
   {
      std::cout << "Error! Please enter valid key size!" << std::endl;
      return -2;
   }

   return 0;
}

static void deletePconf()
{
    OICFree(g_pconf.prm);
    //free pdacl
    OicSecPdAcl_t* acl = g_pconf.pdacls;
    if (acl)
    {
        /* Clean Resources */
        for (unsigned int i = 0; i < (acl)->resourcesLen; i++)
        {
            OICFree((acl)->resources[i]);
        }
        OICFree((acl)->resources);

        /* Clean ACL node itself */
        /* Required only if acl was created in heap */
        OICFree((acl));
    }
    memset(&g_pconf, 0, sizeof(OicSecPconf_t));
}

static OicSecPdAcl_t* InputPdACL()
{
    int ret;
    char *temp_rsc, *temp_pms;

    printf("******************************************************************************\n");
    printf("-Set ACL policy for target DP device\n");
    printf("******************************************************************************\n");

    OicSecPdAcl_t *acl = (OicSecPdAcl_t *)OICCalloc(1,sizeof(OicSecPdAcl_t));
    if (NULL == acl)
    {
        OIC_LOG(ERROR, TAG, "Error while memory allocation");
        return NULL;
    }

    //Set Resource.
    printf("Num. of Resource : ");
    ret = scanf("%" PRIuPTR, &acl->resourcesLen);
    if ((1 != ret) || (acl->resourcesLen <= 0 || acl->resourcesLen > 50))
    {
        printf("Error while input\n");
        OICFree(acl);
        return NULL;
    }
    printf("-URI of resource\n");
    printf("ex)/oic/sh/temp/0 (Max_URI_Length: %d Byte )\n", MAX_URI_LENGTH);
    acl->resources = (char **)OICCalloc(acl->resourcesLen, sizeof(char *));
    if (NULL == acl->resources)
    {
        OIC_LOG(ERROR, TAG, "Error while memory allocation");
        OICFree(acl);
        return NULL;
    }
    for (size_t i = 0; i < acl->resourcesLen; i++)
    {
        printf("[%" PRIuPTR "]Resource : ", i + 1);
        ret = scanf("%64ms", &temp_rsc);
        if (1 != ret)
        {
            printf("Error while input\n");
            OICFree(acl->resources);
            OICFree(acl);
            return NULL;
        }

        acl->resources[i] = OICStrdup(temp_rsc);
        OICFree(temp_rsc);
        if (NULL == acl->resources[i])
        {
            OIC_LOG(ERROR, TAG, "Error while memory allocation");
            OICFree(acl->resources);
            OICFree(acl);
            return NULL;
        }
    }

    // Set Permission
    do
    {
        printf("-Set the permission(C,R,U,D,N)\n");
        printf("ex) CRUDN, CRU_N,..(5 Charaters)\n");
        printf("Permission : ");
        ret = scanf("%5ms", &temp_pms);
        if (1 != ret)
        {
            printf("Error while input\n");
            OICFree(acl->resources);
            OICFree(acl);
            return NULL;
        }
        ret = CalculateAclPermission(temp_pms, &(acl->permission));
        OICFree(temp_pms);
    } while (0 != ret );

    return acl;
}

void provisionDirectPairingCB(PMResultList_t *result, int hasError)
{
    if (hasError)
    {
        std::cout << "Error in provisioning operation!"<<std::endl;
    }
    else
    {
        std::cout<< "\nReceived provisioning results: Direct Pairing is successful ";
        for (unsigned int i = 0; i < result->size(); i++)
        {
            std::cout << "Result is = " << result->at(i).res <<" for device ";
            printUuid(result->at(i).deviceId);
        }

        delete result;
    }
    deletePconf();
    printMenu();
    ask = 1;
}

static void provisionDP(int dev_num)
{
    OCStackResult rst;
    std::string pin("");

    // set enable dp
    g_pconf.edp = true;

    // set default supported PRM types
    g_pconf.prmLen = sizeof(SUPPORTED_PRMS)/sizeof(OicSecPrm_t);
    g_pconf.prm = (OicSecPrm_t *)OICCalloc(g_pconf.prmLen, sizeof(OicSecPrm_t));
    if(g_pconf.prm)
    {
        for (size_t i=0; i < g_pconf.prmLen; i++)
        {
            g_pconf.prm[i] = SUPPORTED_PRMS[i];
        }
    }
    else
    {
        OIC_LOG(ERROR, TAG, "create prm error return");
        goto PVDP_ERROR;
    }

    std::cout << "Enter PIN to be configured: ";
    while (1)
    {
        std::cin >> pin;
        if (pin.length() == DP_PIN_LENGTH)
        {
            break;
        }
        else
        {
            std::cout << "PIN length should be 8, Enter again: ";
        }
    }

    memcpy(g_pconf.pin.val, pin.c_str(), DP_PIN_LENGTH);

    // set default pdacl

    g_pconf.pdacls = InputPdACL();
    if(!g_pconf.pdacls)
    {
        OIC_LOG(ERROR, TAG, "InputPdACL error return");
        goto PVDP_ERROR;
    }

    // call |OCProvisionDirectPairing| API actually
    // calling this API with callback actually acts like blocking
    // for error checking, the return value saved and printed
    rst = pOwnedDevList[dev_num-1]->provisionDirectPairing(&g_pconf, provisionDirectPairingCB);
    if(OC_STACK_OK != rst)
    {
        OIC_LOG_V(ERROR, TAG, "OCProvisionDirectPairing API error: %d", rst);
        if (OC_STACK_UNAUTHORIZED_REQ == rst)
        {
            OIC_LOG(ERROR, TAG, "Target Server NOT Support Direct-Pairing !!! (DPC == false)");
        }
        goto PVDP_ERROR;
    }
    return;

PVDP_ERROR:
    deletePconf();  // after here |acl| points nothing
    ask = 1;
}

OCStackResult displayMutualVerifNumCB(uint8_t mutualVerifNum[MUTUAL_VERIF_NUM_LEN])
{
    OC_UNUSED(mutualVerifNum);
    OIC_LOG(INFO, TAG, "IN displayMutualVerifNumCB");
    OIC_LOG(INFO, TAG, "############ mutualVerifNum ############");
    OIC_LOG_BUFFER(INFO, TAG, mutualVerifNum, MUTUAL_VERIF_NUM_LEN);
    OIC_LOG(INFO, TAG, "############ mutualVerifNum ############");
    OIC_LOG(INFO, TAG, "OUT displayMutualVerifNumCB");
    return OC_STACK_OK;
}

OCStackResult confirmMutualVerifNumCB(void)
{
    for (;;)
    {
        int userConfirm;

        printf("   > Press 1 if the mutual verification numbers are the same\n");
        printf("   > Press 0 if the mutual verification numbers are not the same\n");

        for (int ret=0; 1!=ret; )
        {
            ret = scanf("%d", &userConfirm);
            for (; 0x20<=getchar(); );  // for removing overflow garbage
                                        // '0x20<=code' is character region
        }
        if (1 == userConfirm)
        {
            break;
        }
        else if (0 == userConfirm)
        {
            return OC_STACK_USER_DENIED_REQ;
        }
        printf("   Entered Wrong Number. Please Enter Again\n");
    }
    return OC_STACK_OK;
}




#if defined(__WITH_DTLS__) || defined(__WITH_TLS__)
static int saveTrustCert(void)
{
    // call |OCSaveTrustCertChainBin| API actually
    printf("   Save Trust Cert. Chain into Cred of SVR.\n");

    ByteArray trustCertChainArray = {0, 0};

    FILE *fp = fopen("rootca.crt", "rb+");

    if (fp)
    {
        off_t fsize;
        if (fseeko(fp, 0, SEEK_END) == 0 && (fsize = ftello(fp)) >= 0)
        {
            trustCertChainArray.data = (uint8_t*)OICCalloc(1, fsize);
            trustCertChainArray.len = fsize;
            if (NULL == trustCertChainArray.data)
            {
                OIC_LOG(ERROR,TAG,"Failed to allocate memory");
                fclose(fp);
                return -1;
            }
            rewind(fp);
            if (0 == fread(trustCertChainArray.data, 1, fsize, fp))
            {
                OIC_LOG(ERROR,TAG,"Read error");
            }
        }
        fclose(fp);
    }
    OIC_LOG_BUFFER(DEBUG, TAG, trustCertChainArray.data, trustCertChainArray.len);

    if(OC_STACK_OK != OCSecure::saveTrustCertChain(trustCertChainArray.data, trustCertChainArray.len,
                        OIC_ENCODING_PEM,&g_credId))
    {
        OIC_LOG(ERROR, TAG, "OCSaveTrustCertChainBin API error");
        return -1;
    }
    printf("CredId of Saved Trust Cert. Chain into Cred of SVR : %d.\n", g_credId);

    return 0;
}

void certChainCallBack(uint16_t credId, uint8_t *trustCertChain, size_t chainSize)
{
    OC_UNUSED(credId);
    OC_UNUSED(trustCertChain);
    OC_UNUSED(chainSize);
    OIC_LOG_V(INFO, TAG, "trustCertChain Changed for credId %u", credId);
    return;
}
#endif // __WITH_DTLS__ or __WITH_TLS__

#ifdef MULTIPLE_OWNER
void MOTMethodCB(PMResultList_t *result, int hasError)
{
    if (hasError)
    {
        std::cout << "Error!!! in callback"<<std::endl;
    }
    else
    {
        std::cout<< "callback successfull"<<std::endl;
        delete result;
    }
}
#endif // MULTIPLE_OWNER

class Light
{
public:

    bool m_state;
    int m_power;
    std::string m_name;

    Light() : m_state(false), m_power(0), m_name("")
    {
    }
};

Light mylight;

std::shared_ptr<OCResource> curResource;



// callback handler on PUT request
void onPut(const HeaderOptions& /*headerOptions*/, const OCRepresentation& rep, const int eCode)
{
    try
    {
        if (eCode == OC_STACK_OK || eCode == OC_STACK_RESOURCE_CHANGED)
        {
            std::cout << "PUT request was successful" << std::endl;

            rep.getValue("state", mylight.m_state);
            rep.getValue("power", mylight.m_power);
            rep.getValue("name", mylight.m_name);

            std::cout << "\tstate: " << mylight.m_state << std::endl;
            std::cout << "\tpower: " << mylight.m_power << std::endl;
            std::cout << "\tname: " << mylight.m_name << std::endl;

            //postLightRepresentation(curResource);
        }
        else
        {
            std::cout << "onPut Response error: " << eCode << std::endl;
            //std::exit(-1);
        }
    }
    catch(std::exception& e)
    {
        std::cout << "Exception: " << e.what() << " in onPut" << std::endl;
    }
}

// Local function to put a different state for this resource
void putLightRepresentation(std::shared_ptr<OCResource> resource)
{
    if(resource)
    {
        OCRepresentation rep;

        std::cout << "Putting light representation..."<<std::endl;

        mylight.m_state = true;
        mylight.m_power = 15;

        rep.setValue("state", mylight.m_state);
        rep.setValue("power", mylight.m_power);

        // Invoke resource's put API with rep, query map and the callback parameter
        resource->put(rep, QueryParamsMap(), &onPut);
    }
}

static void printRep(const OCRepresentation& rep) {
	//int ct = 0;
	//for(auto& rep : reps)
	{
		for(auto& attribute : rep)
		{
			std::cout<< "\tAttr "<<attribute.attrname()<<":"
				<< attribute.type()<<" with value " <<attribute.getValueToString() <<std::endl;
		}
		//++ct;
	}

}

// Callback handler on GET request
void onGet(const HeaderOptions& /*headerOptions*/, const OCRepresentation& rep, const int eCode)
{
    try
    {
        if(eCode == OC_STACK_OK)
        {
            std::cout << "GET request was successful" << std::endl;
            std::cout << "Resource URI: " << rep.getUri() << std::endl;

            /*rep.getValue("state", mylight.m_state);
            rep.getValue("power", mylight.m_power);
            rep.getValue("name", mylight.m_name);

            std::cout << "\tstate: " << mylight.m_state << std::endl;
            std::cout << "\tpower: " << mylight.m_power << std::endl;
            std::cout << "\tname: " << mylight.m_name << std::endl;*/
            std::cout << "\tList of resource types: " << std::endl;
            for(auto &resourceTypes : rep.getResourceTypes())
            {
                std::cout << "\t\t" << resourceTypes << std::endl;
            }

			printRep(rep);

            //putLightRepresentation(curResource);
        }
        else
        {
            std::cout << "onGET Response error: " << eCode << std::endl;
            //std::exit(-1);
        }
    }
    catch(std::exception& e)
    {
        std::cout << "Exception: " << e.what() << " in onGet" << std::endl;
    }
}

// Local function to get representation of light resource
void getLightRepresentation(std::shared_ptr<OCResource> resource)
{
    if(resource)
    {
        //std::cout << "Getting Light Representation..."<<std::endl;
        // Invoke resource's get API with the callback parameter

        QueryParamsMap test;
        resource->get(test, &onGet);
    }
}


void foundResource(std::shared_ptr<OCResource> resource)
{
    //std::cout << "In foundResource\n";
    std::string resourceURI;
    std::string hostAddress;

	if(resource)
        {
        	if(resource->sid() != pOwnedDevList[0]->getDeviceID()) {
				//std::cout<<"Ignored as NOT our device"<<std::endl;
				return;
        	}
			std::cout << "In foundResource\n";
#if 0				
            std::cout<<"DISCOVERED Resource:"<<std::endl;
            // Get the resource URI
            resourceURI = resource->uri();
            std::cout << "\tURI of the resource: " << resourceURI << std::endl;

            // Get the resource host address
            hostAddress = resource->host();
            std::cout << "\tHost address of the resource: " << hostAddress << std::endl;

            // Get the resource types
            std::cout << "\tList of resource types: " << std::endl;
            for(auto &resourceTypes : resource->getResourceTypes())
            {
                std::cout << "\t\t" << resourceTypes << std::endl;
            }

            // Get the resource interfaces
            std::cout << "\tList of resource interfaces: " << std::endl;
            for(auto &resourceInterfaces : resource->getResourceInterfaces())
            {
                std::cout << "\t\t" << resourceInterfaces << std::endl;
            }

            // Get Resource current host
            std::cout << "\tHost of resource: " << std::endl;
            std::cout << "\t\t" << resource->host() << std::endl;

            // Get Resource Endpoint Infomation
            std::cout << "\tList of resource endpoints: " << std::endl;
            for(auto &resourceEndpoints : resource->getAllHosts())
            {
                std::cout << "\t\t" << resourceEndpoints << std::endl;
            }
#endif

            // If resource is found from ip based adapter.
            if (std::string::npos != resource->host().find("coap://") ||
                std::string::npos != resource->host().find("coaps://") ||
                std::string::npos != resource->host().find("coap+tcp://") ||
                std::string::npos != resource->host().find("coaps+tcp://"))
            {
                for(auto &resourceEndpoints : resource->getAllHosts())
                {
                    if (resourceEndpoints.compare(resource->host()) != 0 &&
                        std::string::npos == resourceEndpoints.find("coap+rfcomm"))
                    {
                        std::string newHost = resourceEndpoints;

                       /* if (std::string::npos != newHost.find("tcp"))
                        {
                            TRANSPORT_TYPE_TO_USE = OCConnectivityType::CT_ADAPTER_TCP;
                        }
                        else
                        {
                            TRANSPORT_TYPE_TO_USE = OCConnectivityType::CT_ADAPTER_IP;
                        }*/
                        // Change Resource host if another host exists
                        //std::cout << "\tChange host of resource endpoints" << std::endl;
                        //std::cout << "\t\t" << "Current host is "
                                  //<< resource->setHost(newHost) << std::endl;
                        resource->setHost(newHost);
                        break;
                    }
                }
            }

            //if(resourceURI == "/oic/d")
            {
               // if (resource->connectivityType() & TRANSPORT_TYPE_TO_USE)
                {
                    curResource = resource;
                    // Get the resource host address
                    //std::cout << "\tAddress of selected resource: " << resource->host() << std::endl;

                    // Call a local function which will internally invoke get API on the resource pointer
                    getLightRepresentation(resource);
                }
            }
        }
        else
        {
            // Resource is invalid
            std::cout << "Resource is invalid" << std::endl;
        }
}


int main(void)
{
    OCStackResult result;
    OCPersistentStorage ps {client_open, fread, fwrite, fclose, unlink };

    // Create PlatformConfig object
    PlatformConfig cfg {
        OC::ServiceType::InProc,
            OC::ModeType::Both,
            "0.0.0.0",
            0,
            OC::QualityOfService::LowQos,
            &ps
    };

    OCPlatform::Configure(cfg);

    try
    {
        InputPinCallbackHandle callbackHandle = nullptr;
        int choice;
        OicSecAcl_t *acl1 = nullptr, *acl2 = nullptr;
        if (OCSecure::provisionInit("") != OC_STACK_OK)
        {
            std::cout <<"PM Init failed"<< std::endl;
            return 1;
        }

        result = OCSecure::registerInputPinCallback(OnInputPinCB, &callbackHandle);
        if (result != OC_STACK_OK)
        {
            std::cout << "!!Error - registerInputPinCallback failed." << std::endl;
        }

        result = OCSecure::registerDisplayNumCallback(displayMutualVerifNumCB);
        if (result != OC_STACK_OK)
        {
            std::cout<< "!!Error - setDisplayVerifNumCB failed."<<std::endl;
        }

        result = OCSecure::registerUserConfirmCallback(confirmMutualVerifNumCB);
        if (result != OC_STACK_OK)
        {
            std::cout<< "!!Error - setConfirmVerifNumCB failed."<<std::endl;
        }

        for (int out = 0; !out;)
        {
            while (!ask)
            {
                sleep(1);
            }

            if (acl1)
            {
                deleteACL(acl1);
                acl1 = nullptr;
            }

            if (acl2)
            {
                deleteACL(acl2);
                acl2 = nullptr;
            }

            printMenu();
            std::cin >> choice;
            switch(choice) {
                case 1:
                    {
                        //Secure resource discovery.

                        pUnownedDevList.clear();
                        std::cout << "Started discovery..." <<std::endl;
                        OCStackResult result = OCSecure::discoverUnownedDevices(DISCOVERY_TIMEOUT,
                                pUnownedDevList);
                        if (result != OC_STACK_OK)
                        {
                            std::cout<< "!!Error - UnOwned Discovery failed."<<std::endl;
                        }
                        else if (pUnownedDevList.size())
                        {
                            std::cout <<"Found secure devices, count = " <<
                                pUnownedDevList.size() << std::endl;
                            printDevices(pUnownedDevList);
                        }
                        else
                        {
                            std::cout <<"No Secure devices found"<<std::endl;
                        }
                        break;
                    }
                case 2:
                    {
                        pOwnedDevList.clear();
                        std::cout << "Started discovery..." <<std::endl;
                        OCStackResult result = OCSecure::discoverOwnedDevices(DISCOVERY_TIMEOUT,
                                pOwnedDevList);
                        if (result != OC_STACK_OK)
                        {
                            std::cout<< "!!Error - Owned Discovery failed."<<std::endl;
                        }
                        else if (pOwnedDevList.size())
                        {
                            std::cout <<"Found owned devices, count = " <<
                                pOwnedDevList.size() << std::endl;
                            printDevices(pOwnedDevList);
                        }
                        else
                        {
                            std::cout <<"No Secure devices found"<<std::endl;
                        }
                        break;
                    }
                case 3:
                    {
                        unsigned int devNum;

                        if (!pUnownedDevList.size())
                        {
                            std::cout <<"There are no more Unowned devices"<<std::endl;
                            break;
                        }

                        for (unsigned int i = 0; i < pUnownedDevList.size(); i++ )
                        {
                            std::cout << i+1 << ": "<< pUnownedDevList[i]->getDeviceID();
                            std::cout << " From IP:" << pUnownedDevList[i]->getDevAddr() <<std::endl;
                        }

                        std::cout <<"Select device number: "<<std::endl;
                        std::cin >> devNum;
                        if (devNum > pUnownedDevList.size())
                        {
                            std::cout <<"Invalid device number"<<std::endl;
                            break;
                        }
                        transferDevIdx = devNum - 1;
                    
                        ask = 0;
                        std::cout << "Transfering ownership for : "<<
                            pUnownedDevList[devNum-1]->getDeviceID()<<std::endl;
                        if (pUnownedDevList[devNum-1]->doOwnershipTransfer(ownershipTransferCB)
                                != OC_STACK_OK)
                        {
                            std::cout<<"OwnershipTransferCallback is failed"<<std::endl;
                            ask = 1;
                        }
                        break;
                    }
                case 4: //Provision ACL
                    {
                        int index;

                        if (0 != readDeviceNumber(pOwnedDevList, 1, &index)) break;

                        std::cout << "Provision ACL for : "<<
                            pOwnedDevList[index]->getDeviceID()<< std::endl;

                        acl1 = (OicSecAcl_t *)OICCalloc(1,sizeof(OicSecAcl_t));
                        if (NULL == acl1)
                        {
                            OIC_LOG(ERROR, TAG, "Error while memory allocation");
                            break;
                        }

                        std::cout << "Please input ACL for selected device: " << std::endl;
                        if (0 != InputACL(acl1))
                        {
                            break;
                        }

                        ask = 0;

                        if (pOwnedDevList[index]->provisionACL(acl1, provisionCB) != OC_STACK_OK)
                        {
                            ask = 1;
                            std::cout <<"provisionACL is failed"<< std::endl;
                        }
                    }
                    break;
                case 5: //Provision Credentials
                    {
                        int devices[2];

                        if (0 != readDeviceNumber(pOwnedDevList, 2, devices)) break;

                        int first  = devices[0];
                        int second = devices[1];

                        std::cout << "Provision Credentials to devices: "<<
                            pOwnedDevList[first]->getDeviceID();
                        std::cout << " and "<< pOwnedDevList[second]->getDeviceID() << std::endl;

                        Credential cred( NO_SECURITY_MODE ,0);
                        std::cout << "Please input credentials for selected devices: " << std::endl;
                        if (0 != InputCredentials(cred))
                            break;

                        ask = 0;

                        if (pOwnedDevList[first]->provisionCredentials(cred,
                                    *pOwnedDevList[second].get(), provisionCB) != OC_STACK_OK)
                        {
                            ask = 1;
                            std::cout <<"provisionCredentials is failed"<< std::endl;
                        }
                    }
                    break;
                case 6: //Provision ACL & Creds b/w two devices.
                    {
                        int devices[2];

                        if (0 != readDeviceNumber(pOwnedDevList, 2, devices)) break;

                        int first  = devices[0];
                        int second = devices[1];

                        std::cout << "Provision pairwise devices: "<<
                            pOwnedDevList[first]->getDeviceID();
                        std::cout << " and "<< pOwnedDevList[second]->getDeviceID() << std::endl;

                        Credential cred( NO_SECURITY_MODE, 0);
                        std::cout << "Please input credentials for selected devices: " << std::endl;
                        if (0 != InputCredentials(cred))
                            break;

                        acl1 = (OicSecAcl_t *)OICCalloc(1,sizeof(OicSecAcl_t));
                        if (NULL == acl1)
                        {
                            OIC_LOG(ERROR, TAG, "Error while memory allocation");
                            break;
                        }

                        std::cout << "Please input ACL for selected device: " << std::endl;
                        if (0 != InputACL(acl1))
                        {
                            break;
                        }

                        acl2 = (OicSecAcl_t *)OICCalloc(1,sizeof(OicSecAcl_t));
                        if (NULL == acl2)
                        {
                            OIC_LOG(ERROR, TAG, "Error while memory allocation");
                            break;
                        }

                        std::cout << "Please input ACL for selected device: " << std::endl;
                        if (0 != InputACL(acl2))
                        {
                            break;
                        }

                        ask = 0;

                        if (pOwnedDevList[first]->provisionPairwiseDevices(cred, acl1,
                                    *pOwnedDevList[second].get(), acl2, provisionCB) != OC_STACK_OK)
                        {
                            ask = 1;
                            std::cout <<"provisionPairwiseDevices is failed"<< std::endl;
                        }
                    }
                    break;
                case 7: //Unlink Devices
                    {
                        int devices[2];

                        if (0 != readDeviceNumber(pOwnedDevList, 2, devices)) break;

                        int first  = devices[0];
                        int second = devices[1];

                        std::cout << "Unlink devices: "<< pOwnedDevList[first]->getDeviceID();
                        std::cout << " and "<< pOwnedDevList[second]->getDeviceID() << std::endl;

                        ask = 0;

                        if (pOwnedDevList[first]->unlinkDevices(*pOwnedDevList[second].get(),
                                    provisionCB) != OC_STACK_OK)
                        {
                            ask = 1;
                            std::cout <<"unlinkDevice is failed"<< std::endl;
                        }
                        break;
                    }
                case 8: //Remove Device
                    {
                        int index;

                        if (0 != readDeviceNumber(pOwnedDevList, 1, &index)) break;

                        std::cout << "Remove Device: "<< pOwnedDevList[index]->getDeviceID()<< std::endl;

                        ask = 0;

                        if (pOwnedDevList[index]->removeDevice(DISCOVERY_TIMEOUT, provisionCB)
                                != OC_STACK_OK)
                        {
                            ask = 1;
                            std::cout <<"removeDevice is failed"<< std::endl;
                        }
                        break;
                    }
                case 9: //Remove Device using UUID
                    {
                        int index;

                        if (0 != readDeviceNumber(pOwnedDevList, 1, &index)) break;

                        std::cout << "Remove Device: "<< pOwnedDevList[index]->getDeviceID()<< std::endl;

                        ask = 0;

                        if (OCSecure::removeDeviceWithUuid(DISCOVERY_TIMEOUT,
                                                                       pOwnedDevList[index]->getDeviceID(),
                                                                       provisionCB)
                                != OC_STACK_OK)
                        {
                            ask = 1;
                            std::cout <<"removeDevice is failed"<< std::endl;
                        }
                        break;
                    }

                case 10: //Get Linked devices
                    {
                        UuidList_t linkedUuid;
                        unsigned int devNum;

                        if (!pOwnedDevList.size())
                        {
                            std::cout <<"There are no Owned devices yet,"
                                " may need to discover"<<std::endl;
                            break;
                        }

                        for (unsigned int i = 0; i < pOwnedDevList.size(); i++ )
                        {
                            std::cout << i+1 << ": "<< pOwnedDevList[i]->getDeviceID() <<" From IP:";
                            std::cout << pOwnedDevList[i]->getDevAddr() <<std::endl;
                        }

                        std::cout <<"Select device number: "<<std::endl;
                        std::cin >> devNum;
                        if (devNum > pOwnedDevList.size())
                        {
                            std::cout <<"Invalid device number"<<std::endl;
                            break;
                        }

                        if(pOwnedDevList[devNum  -1]->getLinkedDevices(linkedUuid) == OC_STACK_OK)
                        {
                            if (!linkedUuid.size())
                            {
                                std::cout <<"No devices are linked to "<<
                                    pOwnedDevList[devNum  -1]->getDeviceID() << std::endl;
                            }
                            //display the Linked devices (UUIDs)
                            for(unsigned int i = 0; i < linkedUuid.size(); i++)
                            {
                                printUuid(linkedUuid[i]);
                            }
                        }
                        else
                        {
                            std::cout <<"Error! in getLinkedDevices"<<std::endl;
                        }
                        break;
                    }
                case 11: //Get device' status
                    {
                        DeviceList_t unownedList, ownedList;

                        if (OCSecure::getDevInfoFromNetwork(DISCOVERY_TIMEOUT, ownedList,
                                    unownedList) == OC_STACK_OK)
                        {
                            std::cout <<"Owned Device' status for" <<std::endl;
                            for (unsigned int i = 0; i < ownedList.size(); i++ )
                            {
                                std::cout << "Device "<<i+1 <<" ID: '";
                                std::cout << ownedList[i]->getDeviceID() << "' From IP: ";
                                std::cout << ownedList[i]->getDevAddr() << " Status: ";
                                printStatus(ownedList[i]->getDeviceStatus());
                            }
                            std::cout <<"\nUnOwned Device' status for" <<std::endl;
                            for (unsigned int i = 0; i < unownedList.size(); i++ )
                            {
                                std::cout << "Device "<<i+1 <<" ID: '";
                                std::cout << unownedList[i]->getDeviceID() << "' From IP: ";
                                std::cout << unownedList[i]->getDevAddr() << " Status: ";
                                printStatus(unownedList[i]->getDeviceStatus());
                            }

                        }
                        break;
                    }

                case 12:
                    {
                        unsigned int devNum;

                        if (!pOwnedDevList.size())
                        {
                            std::cout <<"There are no Owned devices yet,"
                                " may need to discover"<<std::endl;
                            break;
                        }

                        for (unsigned int i = 0; i < pOwnedDevList.size(); i++ )
                        {
                            std::cout << i+1 << ": "<< pOwnedDevList[i]->getDeviceID() <<" From IP:";
                            std::cout << pOwnedDevList[i]->getDevAddr() <<std::endl;
                        }

                        std::cout <<"Select device number: "<<std::endl;
                        std::cin >> devNum;
                        if (devNum > pOwnedDevList.size())
                        {
                            std::cout <<"Invalid device number"<<std::endl;
                            break;
                        }

                        ask = 0;
                        provisionDP(devNum);

                        break;
                    }
#if defined(__WITH_DTLS__) || defined(__WITH_TLS__)
                case 13:
                    {
                        std::cout<< "registering cert chain change notifier"<<std::endl;
                        OCSecure::registerTrustCertChangeNotifier(certChainCallBack);
                        if(saveTrustCert())
                        {
                            std::cout<<"Error in saving cert"<<std::endl;
                        }
                        std::cout<< "Unregister notifier"<<std::endl;
                        OCSecure::removeTrustCertChangeNotifier();
                        break;
                    }
                case 14:
                    {
                        int index;

                        if (0 != readDeviceNumber(pOwnedDevList, 1, &index)) break;

                        std::cout << "Provision cert for : "<<
                            pOwnedDevList[index]->getDeviceID()<< std::endl;

                        ask = 0;

                        if (pOwnedDevList[index]->provisionTrustCertChain(SIGNED_ASYMMETRIC_KEY,
                                                                    g_credId,provisionCB ) != OC_STACK_OK)
                        {
                            ask = 1;
                            std::cout <<"provision cert is failed"<< std::endl;
                        }
                        break;
                    }
                case 15:
                    {
                        if (0==g_credId)
                        {
                            std::cout<<"please save cert using option 13.";
                        }
                        else
                        {
                            uint8_t *trustCertChain = NULL;
                            size_t chainSize = 0;
                            if (OC_STACK_OK != OCSecure::readTrustCertChain(g_credId, &trustCertChain,&chainSize))
                            {
                                std::cout <<"issue in read trust chain"<< std::endl;
                            }
                            else
                            {
                                std::cout<<"size of cert : "<<chainSize<<std::endl;
                            }
                        }
                        break;
                    }
#endif //__WITH_DTLS__ || __WITH_TLS__
#ifdef MULTIPLE_OWNER
                 case 16:
                    {
                        if (!pOwnedDevList.size() && !pMOTEnabledDeviceList.size())
                        {
                            std::cout <<"Owned device list and MOT device list both are empty."<<std::endl;
                            break;
                        }
                        unsigned int dev_count = 0;
                        if (pOwnedDevList.size())
                        {
                            dev_count = pOwnedDevList.size();
                            printDevices(pOwnedDevList);
                        }

                        if (pMOTEnabledDeviceList.size())
                        {
                            dev_count += pMOTEnabledDeviceList.size();
                            for (unsigned int i = 0; i < pMOTEnabledDeviceList.size(); i++ )
                            {
                                std::cout << "Device ";
                                std::cout <<((dev_count - pMOTEnabledDeviceList.size())+ i + 1) ;
                                std::cout <<" ID : ";
                                std::cout << pMOTEnabledDeviceList[i]->getDeviceID()<<" From IP: ";
                                std::cout << pMOTEnabledDeviceList[i]->getDevAddr() << std::endl;
                            }
                        }

                        // select device
                        unsigned int dev_num = 0;
                        for( ; ; )
                        {
                            std::cout << "Enter Device Number, to change the mode: "<<std::endl;
                            std::cin >> dev_num;
                            if(0 < dev_num && dev_count >= dev_num)
                            {
                                break;
                            }
                            std::cout << "   Entered Wrong Number. Please Enter Again"<<std::endl;
                        }

                        OicSecMomType_t momType = OIC_MULTIPLE_OWNER_ENABLE;
                        int mom = 0;
                        for( ; ; )
                        {
                            std::cout <<"   0. Disable Multiple Ownership Transfer"<<std::endl;
                            std::cout <<"   1. Enable Multiple Ownership Transfer "<<std::endl;
                            std::cout <<"> Enter Mode of Multiple Ownership Transfer :"<<std::endl;
                            for(int ret=0; 1!=ret; )
                            {
                                ret = scanf("%d", &mom);
                                for( ; 0x20<=getchar(); );  // for removing overflow garbages
                                                            // '0x20<=code' is character region
                            }
                            if(mom == 0)
                            {
                                momType = OIC_MULTIPLE_OWNER_DISABLE;
                                break;
                            }
                            if(mom == 1)
                            {
                                momType = OIC_MULTIPLE_OWNER_ENABLE;
                                break;
                            }
                            std::cout <<"     Entered Wrong Number. Please Enter Again"<<std::endl;
                        }

                        if (!pOwnedDevList.size())
                        {
                            if(OC_STACK_OK != pMOTEnabledDeviceList[dev_num-1]->changeMOTMode(
                            (const OicSecMomType_t)momType,MOTMethodCB))
                            {
                                OIC_LOG(ERROR, TAG, "changeMOTMode API error");
                            }
                        }
                        else
                        {
                            if(dev_num <= pOwnedDevList.size())
                            {
                                if(OC_STACK_OK != pOwnedDevList[dev_num-1]->changeMOTMode(momType,
                                MOTMethodCB))
                                {
                                    OIC_LOG(ERROR, TAG, "changeMOTMode API error");
                                }
                            }
                            else
                            {
                                if(OC_STACK_OK != pMOTEnabledDeviceList[(dev_num -
                                pOwnedDevList.size() - 1)]->changeMOTMode(momType,
                                MOTMethodCB))
                                {
                                    OIC_LOG(ERROR, TAG, "changeMOTMode API error");
                                }
                            }
                        }
                         break;
                    }
                case 17:
                    {
                        if (!pMOTEnabledDeviceList.size())
                        {
                            std::cout <<"Please discover the MOT device first. Use option 18"<<std::endl;
                            break;
                        }

                        printDevices(pMOTEnabledDeviceList);
                        // select device
                        unsigned int dev_num = 0;
                        for( ; ; )
                        {
                            std::cout << "Enter Device Number, for MOT Device: "<<std::endl;
                            std::cin >> dev_num;
                            if(0 < dev_num &&  pMOTEnabledDeviceList.size() >=dev_num)
                            {
                                break;
                            }
                                              std::cout << "     Entered Wrong Number. Please Enter Again"<<std::endl;
                        }

                        int oxm = 0;
                        OicSecOxm_t secOxm = OIC_PRECONFIG_PIN;
                        std::cout << "Select method for  Multiple Ownership Transfer: "<<std::endl;
                        for( ; ; )
                        {
                            std::cout << "  0. Random PIN OxM "<<std::endl;
                            std::cout << "  1. Pre-Configured PIN OxM "<<std::endl;
                            std::cout << "   > Enter Number of  OxM for Multiple Ownership Transfer : "<<std::endl;
                            std::cin >> oxm;
                            if(0 == oxm)
                            {
                                secOxm = OIC_RANDOM_DEVICE_PIN;
                                break;
                            }
                            if(1 == oxm)
                            {
                                secOxm = OIC_PRECONFIG_PIN;
                                break;
                            }
                            std::cout << "     Entered Wrong Number. Please Enter Again"<<std::endl;
                        }

                        if(OC_STACK_OK != pMOTEnabledDeviceList[dev_num-1]->selectMOTMethod((const OicSecOxm_t)secOxm,
                                              MOTMethodCB))
                        {
                            OIC_LOG(ERROR, TAG, "selectMOTMethod API error");
                        }
                         break;
                    }
                case 18:
                    {
                        pMOTEnabledDeviceList.clear();
                        std::cout << "Started MOT Enabled device discovery..." <<std::endl;
                        OCStackResult result = OCSecure::discoverMultipleOwnerEnabledDevices
                            (DISCOVERY_TIMEOUT, pMOTEnabledDeviceList);
                        if (result != OC_STACK_OK)
                        {
                            std::cout<< "!!Error - MOT Enabled dev Discovery failed."<<std::endl;
                        }
                        else if (pMOTEnabledDeviceList.size())
                        {
                            std::cout <<"Found MOT enabled devices, count = " <<
                                pMOTEnabledDeviceList.size() << std::endl;
                            printDevices(pMOTEnabledDeviceList);
                        }
                        else
                        {
                            std::cout <<"No MOT enabled Secure devices found"<<std::endl;
                        }
                        break;
                    }
                case 19:
                    {
                        if (!pMOTEnabledDeviceList.size())
                        {
                            std::cout <<"Please discover the MOT device first. Use option 16"<<std::endl;
                            break;
                        }

                        printDevices(pMOTEnabledDeviceList);
                        // select device
                        unsigned int dev_num = 0;
                        for( ; ; )
                        {
                            std::cout << "Enter Device Number, for MOT Device: "<<std::endl;
                            std::cin >> dev_num;
                            if(0 < dev_num && pMOTEnabledDeviceList.size() >=dev_num)
                            {
                                break;
                            }
                            std::cout << "     Entered Wrong Number. Please Enter Again"<<std::endl;
                        }

                        char preconfigPin[9] = {0};
                        std::cout << "   > Input the 8 digit PreconfigPin (e.g. 12341234) :" <<std::endl;
                        for(int ret=0; 1!=ret; )
                        {
                            ret = scanf("%8s", preconfigPin);
                            for( ; 0x20<=getchar(); );  // for removing overflow garbages
                                                        // '0x20<=code' is character region
                        }
                        size_t preconfPinLength = strlen(preconfigPin);
                        if(OC_STACK_OK != pMOTEnabledDeviceList[dev_num-1]->provisionPreconfPin(preconfigPin,
                            preconfPinLength, MOTMethodCB))
                        {
                            OIC_LOG(ERROR, TAG, "provisionPreconfPin API error");
                        }
                        break;
                    }
                    case 20:
                    {
                        if (!pMOTEnabledDeviceList.size())
                        {
                            std::cout <<"Please discover the MOT device first. Use option 16"<<std::endl;
                            break;
                        }

                        printDevices(pMOTEnabledDeviceList);
                        // select device
                        unsigned int dev_num = 0;
                        for( ; ; )
                        {
                            std::cout << "Enter Device Number, for MOT Device: "<<std::endl;
                            std::cin >> dev_num;
                            if(0 < dev_num && pMOTEnabledDeviceList.size() >=dev_num)
                            {
                                break;
                            }
                            std::cout << "     Entered Wrong Number. Please Enter Again"<<std::endl;
                        }

                        char preconfPIN[9] = {0};
                        std::cout << "   > Input the 8 digit preconfPIN (e.g. 12341234) :" <<std::endl;
                        for(int ret=0; 1!=ret; )
                        {
                            ret = scanf("%8s", preconfPIN);
                            for( ; 0x20<=getchar(); );  // for removing overflow garbages
                                                        // '0x20<=code' is character region
                        }
                        size_t preconfPinLength = strlen(preconfPIN);
                        if(OC_STACK_OK != pMOTEnabledDeviceList[dev_num-1]->addPreconfigPIN(preconfPIN,
                            preconfPinLength))
                        {
                            OIC_LOG(ERROR, TAG, "addPreconfigPIN API error");
                        }
                        break;
                    }
#endif //MULTIPLE_OWNER
                case 21:
                    {
                        OCStackResult result;
                        result = OCSecure::configSelfOwnership();
                        if (OC_STACK_OK != result)
                        {
                            std::cout<<"configSelfOwnership API error. Please check SVR DB"<<std::endl;
                        }
                        else
                        {
                            std::cout<<"Success to configures SVR DB as self-ownership"<<std::endl;
                        }
                        break;
                    }
				case 30:
					{
						// Find all resources
						std::ostringstream requestURI;
				        //requestURI << OC_RSRVD_WELL_KNOWN_URI<< "?rt=core.led";// << "?rt=core.light";
				        //requestURI << OC_RSRVD_WELL_KNOWN_URI << "?rt=core.light";
				        requestURI << OC_RSRVD_WELL_KNOWN_URI;// << "?rt=core.light";

				        OCPlatform::findResource("", requestURI.str(),
				                CT_DEFAULT, &foundResource);
					}
				case 31:
					{
						// Find all resources
						std::ostringstream requestURI;
				        //requestURI << OC_RSRVD_WELL_KNOWN_URI<< "?rt=core.led";// << "?rt=core.light";
				        requestURI << OC_RSRVD_WELL_KNOWN_URI << "?rt=core.light";

				        OCPlatform::findResource("", requestURI.str(),
				                CT_DEFAULT, &foundResource);
					}
				break;
                case 99:
                default:
                    out = 1;
                    break;
            }
        }

        // Unregister the input pin callback
        OCSecure::deregisterInputPinCallback(callbackHandle);
    }
    catch(OCException& e)
    {
        oclog() << "Exception in main: "<<e.what();
    }

    return 0;
}
