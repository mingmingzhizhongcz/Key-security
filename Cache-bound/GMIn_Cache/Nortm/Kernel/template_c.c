#include <linux/crypto.h>
#include <linux/mm.h>
#include <linux/highmem.h>
#include <linux/ioctl.h>
#include <linux/syscalls.h>
#include <linux/pagemap.h>
#include <linux/scatterlist.h>
#include <linux/version.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <asm/io.h>
#include <linux/syscalls.h>
#include <linux/sysfs.h>
#include <linux/tty.h>
#include <stdarg.h>
#include <linux/vt_kern.h>
#include <asm/cacheflush.h>
#include <asm/pgtable.h>
#include <linux/ioport.h>
#include <asm/io.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/delay.h>
#include <asm/mtrr.h>
#include <linux/stop_machine.h>
#include <linux/slab.h>
#include <linux/timer.h>
#include <linux/rtc.h>


#include <linux/rcupdate.h>
#include <linux/fdtable.h>
#include <linux/dcache.h>
#include <linux/vmalloc.h>

// get_file_path
#include <linux/sched.h>
#include <linux/uaccess.h>
#include <linux/fs.h>
#include <linux/fs_struct.h>

#include "SMS4.h"
#include "sm3hash.h"
#include "EllipticCurve.h"
#include "rtm.h"
#include "ioc.h"
#include "eltt2.h"
#include "pbkdf.h"
#include "tsx.h"

MODULE_AUTHOR("<DACAS>");
MODULE_DESCRIPTION("Crypto engine driver");
MODULE_LICENSE("GPL");

void reg_init(void *key);

struct Template_dev{
	struct miscdevice *cdev;		
};
static struct Template_dev template_dev;

static long template_ioctl(struct file *, unsigned int, unsigned long);
static const struct file_operations template_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = template_ioctl,
};
static struct miscdevice innerDev = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "nortm",
	.fops = &template_fops,
};

static void __init init_template_dev(void){
	template_dev.cdev = &innerDev;
}
char *module_name = "nortm";

//module_param(debug, bool,S_IRUSR);

char buff[65535];

void printhex(unsigned char * output, int len){
    int i = 0, loc = 0;
    // for(i = 0; i < len; i++)
    // {
    //     printk(" %02x", output[i]);
    // }
	// printk(" \n");
	for(i = 0; i < len; i++)
    {
        loc += sprintf(buff+loc, " %02x", output[i]);
    }
	buff[loc] = 0;
	printk("%s\n", buff);
	
}

void printinfohex(char *info, unsigned char * output, int len){
	printk("%s \n", info);
	printhex(output, len);
}

/*obtain the path of the type (ko, log or pin.list)
 *the path is returned in the var file
 */
void get_file_path(char *file, char *type){
	char *dir=NULL;
	struct path pwd, root; 
	char buf[100];
	pwd = current->fs->pwd;
	path_get(&pwd);
	root=  current->fs->root;
	path_get(&root);
	dir = d_path(&pwd,buf,100*sizeof(char));
	strcpy(file, dir);
	strcat(file, "/");
	// if(strncmp(type,".ko",3)==0)
	strcat(file, module_name);
	strcat(file, type);
}

/*write the log information into LOG_FILE
*return value: 1 for success, 0 for fail
*/
int WriteLogFile(char * loginfo){
	char logPath[100];
	struct timex txc;
	struct rtc_time tm;
	struct file *file_st;
	mm_segment_t fs;
	unsigned char buf[128];
	int len;
	// open log file
	get_file_path(logPath,".log");
	file_st = filp_open(logPath, LOG_FLAGS, LOG_PERMISSION);
	if(IS_ERR(file_st)){
		printk("Error:Can't open log file\n");
		return 0;
	}
	fs = get_fs(); 
	set_fs(KERNEL_DS);
	// write current time to log file
	do_gettimeofday(&(txc.time));
	rtc_time_to_tm(txc.time.tv_sec,&tm);
	len = snprintf(buf, 128,"[UTC time:%d-%d-%d %d:%d:%d]\t", tm.tm_year+1900, tm.tm_mon+1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
	kernel_write(file_st, buf, len, &(file_st->f_pos));

	memset(buf,0x0,128);
	len = snprintf(buf,128,loginfo);
	kernel_write(file_st, buf, len, &(file_st->f_pos));
	memset(buf,0x0,128);
	set_fs(fs);
	filp_close(file_st, NULL);
	return 1;
}

/*if the input PIN is correct, return 1, else return 0*/
int check_pin(unsigned char *pin){
	unsigned char in[PIN_LEN+SALT_LEN];
	unsigned char out[32];
	if(PIN_Error >= MAX_PIN_ERROR)
	{	
		if(PIN_Error <=  MAX_PIN_ERROR+3)
		{
			PIN_Error++;
			WriteLogFile("Input PIN exceeds the max, contact the administor\n");
		}
		return 0;
	}
	memcpy(in, pin, PIN_LEN);
	memcpy(in+PIN_LEN, salt, SALT_LEN);
	Sm3Hash(out, in, PIN_LEN+SALT_LEN);
	if(memcmp(out, pin_list, 32)==0)
	{
		PIN_Error = 0;
		return 1;	
	}
	PIN_Error++;
	WriteLogFile("Input PIN is error\n");
	return 0;
}


/******************************sign module*************************************/
/*in: Public Key for verifying the signature
*out: 
* --digest: the SM3 digest of the module file (including the public key information) for verifying the signature
* --return value : 1 for success, 0 for error
*description: the module file (.ko) is obtained through the path MOD_FILE
*/
int get_mod_digest(CECCPublicKey *pk, unsigned char* digest){
	unsigned char user_name[USER_NAME_SIZE] = {0x00,0xe5,0x8b,0xaf,0xf7,0x7e,0x6e,0x3d,0x4a,0xa1,0x3f,0x49,0xb5,0x28,0xcc,0xc1,0xe1,0x0c,0xa1,0x62,0x9e,0xcf,0x08,0xd9,0x41,0xe4,0xd4,0x0a,0x76,0x76,0xe1,0x4c};
	unsigned char *mod_message = NULL;
	char modulePath[100];
	int count;
	HASH_STATE hashState;
	struct file *file_st;
	mm_segment_t fs;
	get_file_path(modulePath,".ko");
	file_st = filp_open(modulePath, MOD_FLAGS, MOD_PERMISSION);
	if(IS_ERR(file_st)){
		printk("Error: Can't open module file (.ko) \n");
		return 0;
	}
	fs = get_fs(); 
	set_fs(KERNEL_DS);
	PubHashUserId(pk, digest, user_name, USER_NAME_SIZE);
	HashInit(&hashState, digest, HASH_256);

	mod_message = vmalloc(1024);
	if(mod_message == NULL)
		return 0;

	do{
		count = kernel_read(file_st, mod_message, 1024,&(file_st->f_pos));
		HashPending(&hashState, mod_message, count);
	}while(count>0);
	HashFinal(digest, &hashState);
	set_fs(fs);
	filp_close(file_st, NULL);

	vfree(mod_message);
	return 1;
}
/*
*verify the code integrity of the module file (.ko), 
*the public key is specified in the code (x,y), the signature is obtained from the file (path is SIGN_FILE)
*return value: 1 for success, 0 for error
*/
int verify_mod_sign(void){
	CECCPublicKey pk;
	unsigned char x[] = {0x03,0x59,0xe1,0x94,0x35,0xa6,0x07,0x98,0xcd,0x43,0x12,0x04,0x04,0x40,0xfc,0x9d,0xbf,0x13,0x8a,0xfa,0x06,0xe2,0x71,0x0c,0x24,0xc2,0x5b,0x27,0x69,0x5b,0x37,0xf8};
	unsigned char y[] = {0x75,0x5c,0xc5,0x56,0x75,0x52,0x78,0xeb,0xc9,0x64,0x7f,0xa7,0xff,0x58,0x2e,0xae,0xd7,0x66,0xb1,0x58,0x76,0x37,0x68,0xb4,0x53,0x3b,0x45,0x8d,0x38,0xe1,0xd4,0xee};
	unsigned char sign[256];
	int sign_size;
	unsigned char mod_digest[HASH_256];
	int ret;
	struct file *file_st;
	mm_segment_t fs;
	
	memset(sign, 0x0, 256);
	memset(mod_digest, 0x0, HASH_256);
	
	// read sign from file
	file_st = filp_open(SIGN_FILE, SIGN_FLAGS, SIGN_PERMISSION);
	if(IS_ERR(file_st)){
		printk("Error: Can't open sign file \n");
		return 0;
	}
	fs = get_fs(); 
	set_fs(KERNEL_DS); 
	sign_size = kernel_read(file_st, sign, 256,&(file_st->f_pos));
	set_fs(fs);
	filp_close(file_st, NULL);
	// init public key
	CMpiInit(&(pk.m_pntPx));
	CMpiInit(&(pk.m_pntPy));
	CMpiInport(&(pk.m_pntPx), x, 32);
	CMpiInport(&(pk.m_pntPy), y, 32);
	// read mod from file
	get_mod_digest(&pk, mod_digest);
	// verify sign
	ret = Verify(&pk, mod_digest, HASH_256, sign, sign_size);
	if(ret==0){
		printk("Error: code integrity check\n");
		WriteLogFile("Error: code integrity check\n");
	}else{
		printk("Success: code integrity check\n");
		WriteLogFile("Success: code integrity check\n");
	}
	return ret;	
}

/******************************init module*************************************/
/*
*generate the master key from the input masterKey
* PBKDF2 with the salt (specified in code) for 1000 times to generate the 128 bit master key
* the master key is set in each cpu
* 1 for success, 0 for fail
*/
int init_hsm(unsigned char *key){
	
	unsigned char outtemp[16];
	unsigned char salt[8] = {0x1,0x2,0x3,0x3,0x5,0x6,0x7,0x8};
	int count = 1000;

	if(Inited!=0){
	 	printk(KERN_INFO "MASTER_KEY Already Inited\n");
	 	WriteLogFile("MASTER_KEY Already Inited\n");
	 	return 1;
	}
	PBKDF2(outtemp, key, MASTER_KEY_SIZE, salt, 8, count, 16);
	on_each_cpu(reg_init,(void*)outtemp,1);

	//cleanup
	memset(key, 0x0, MASTER_KEY_SIZE);
	memset(outtemp, 0x0, 16);
	Inited = 1;
	WriteLogFile("Success: Init MAKTER_KEY\n");
	return 1;
}


/******************************test function*************************************/
int SM2_self_test(void){
	CECCPrivateKey sk;
	SM2_Para *testsm2 = vmalloc(sizeof(SM2_Para));
	unsigned char sm2cipher[116] = {0x04, 0x04, 0xeb, 0xfc, 0x71, 0x8e, 0x8d, 0x17, 0x98, 0x62, 0x4, 0x32, 0x26, 0x8e, 0x77, 0xfe, 0xb6, 0x41, 0x5e, 0x2e, 0xde, 0xe, 0x7, 0x3c, 0xf, 0x4f, 0x64, 0xe, 0xcd, 0x2e, 0x14, 0x9a, 0x73, 0xe8, 0x58, 0xf9, 0xd8, 0x1e, 0x54, 0x30, 0xa5, 0x7b, 0x36, 0xda, 0xab, 0x8f, 0x95, 0xa, 0x3c, 0x64, 0xe6, 0xee, 0x6a, 0x63, 0x9, 0x4d, 0x99, 0x28, 0x3a, 0xff, 0x76, 0x7e, 0x12, 0x4d, 0xf0, 0x59, 0x98, 0x3c, 0x18, 0xf8, 0x9, 0xe2, 0x62, 0x92, 0x3c, 0x53, 0xae, 0xc2, 0x95, 0xd3, 0x3, 0x83, 0xb5, 0x4e, 0x39, 0xd6, 0x9, 0xd1, 0x60, 0xaf, 0xcb, 0x19, 0x8, 0xd0, 0xbd, 0x87, 0x66, 0x21, 0x88, 0x6c, 0xa9, 0x89, 0xca, 0x9c, 0x7d, 0x58, 0x8, 0x73, 0x7, 0xca, 0x93, 0x9, 0x2d, 0x65, 0x1e, 0xfa}; 
	unsigned char pRandomUser[32] = {0x39,0x45,0x20,0x8f,0x7b,0x21,0x44,0xb1,0x3f,0x36,0xe3,0x8a,0xc6,0xd3,0x9f,0x95,0x88,0x93,0x93,0x69,0x28,0x60,0xb5,0x1a,0x42,0xfb,0x81,0xef,0x4d,0xf7,0xc5,0xb8};
	unsigned char sm2_tar[19] = {0x65,0x6e,0x63,0x72,0x79,0x70,0x74,0x69,0x6f,0x6e,0x20,0x73,0x74,0x61,0x6e,0x64,0x61,0x72,0x64};

	testsm2->len = 116;
	memcpy(testsm2->d,pRandomUser,32);
	memcpy(testsm2->cipher,sm2cipher,116);
	memset(testsm2->plain,0,SM2_MAX_PLAIN_LEN);
	CMpiInit(&(sk.m_pntPx)); CMpiInit(&(sk.m_pntPy)); CMpiInit(&(sk.m_paramD));
	GenerateKey(&sk,testsm2->d,32);
	DecryptMessage(&sk,testsm2->plain,testsm2->cipher+1,testsm2->len-1);

	if(strncmp(sm2_tar, testsm2->plain, 19)!=0)
	{
		vfree(testsm2);
		WriteLogFile("SM2 decryption self test fail\n");
		return 0;
	}	
	vfree(testsm2);
	return 1;
}

int SM3_self_test(void){
	unsigned char sm3plain[3] = {0x61,0x62,0x63};
	unsigned char sm3_tar[32] = {0x66,0xc7,0xf0,0xf4,0x62,0xee,0xed,0xd9,0xd1,0xf2,0xd4,0x6b,0xdc,0x10,0xe4,0xe2,0x41,0x67,0xc4,0x87,0x5c,0xf2,0xf7,0xa2,0x29,0x7d,0xa0,0x2b,0x8f,0x4b,0xa8,0xe0};
	unsigned char sm3_digest[32];

	Sm3Hash(sm3_digest, sm3plain, 3);
	if(strncmp(sm3_tar, sm3_digest, 32)!=0)
	{
		WriteLogFile("fail:SM3 self test failed\n");
		return 0;
	}
	return 1;
}

int SM4_self_test(void){
	unsigned char sm4plain[16] = {0x01, 0x23, 0x45, 0x67,0x89,0xAB,0xCD,0xEF,0xFE,0xDC,0xBA,0x98,0x76,0x54,0x32,0x10};
	unsigned char sm4key[16] = {0x01, 0x23, 0x45, 0x67,0x89,0xAB,0xCD,0xEF,0xFE,0xDC,0xBA,0x98,0x76,0x54,0x32,0x10};
	unsigned char sm4cipher[16] = {0x68,0x1E,0xDF,0x34,0xD2,0x06,0x96,0x5E,0x86,0xB3,0xE9,0x4F,0x53,0x6E,0x42,0x46};
	unsigned char sm4_tar[16];

	SM4EncryptWithMode(sm4plain, 16, sm4_tar, 16, NULL, ECB, sm4key);
	if(strncmp(sm4_tar, sm4cipher, 16)!=0)
	{
		WriteLogFile("fail:SM4 enc self test failed\n");
		return 0;
	}

	SM4DecryptWithMode(sm4cipher, 16, sm4_tar, 16, NULL, ECB, sm4key);
	if(strncmp(sm4_tar, sm4plain, 16)!=0)
	{
		WriteLogFile("fail:SM4 dec self test failed\n");
		return 0;
	}
	return 1;
}

/*
*self test
* SM2()
* SM3()
* SM4()
* random()
* 1 for success, 0 for fail
*/
int self_test(void){
		
	if((SM2_self_test()!=1) || (SM3_self_test()!=1) || (SM4_self_test()!=1))
		return 0;
	ServiceAvailable = 1;
	WriteLogFile("Success:SELF TEST pass\n");
	return 1;
}

/*encrypt the message
* success: 1, fail :0
*/
int sm2_enc_test(SM2_Para *sm2para_u){
	int rc;
	CECCPublicKey pk;
	CMpi x,y;	
	SM2_Para *sm2para_k = NULL;		
	unsigned char kk[32] = {0x59,0x27,0x6E,0x27,0xD5,0x06,0x86,0x1A,0x16,0x68,0x0F,0x3A,0xD9,0xC0,0x2D,0xCC,0xEF,0x3C,0xC1,0xFA,0x3C,0xDB,0xE4,0xCE,0x6D,0x54,0xB8,0x0D,0xEA,0xC1,0xBC,0x21};
	//get_random_bytes(kk, 32);

	sm2para_k = vmalloc(sizeof(SM2_Para));
	if(sm2para_k == NULL)
		return 0;

	if(copy_from_user((unsigned char*)(sm2para_k->pin), (unsigned char*)(sm2para_u->pin), PIN_LEN)){
		vfree(sm2para_k);
		return 0;
	}
	if(check_pin(sm2para_k->pin)==0) {
		vfree(sm2para_k);
		return 0;
	}

	/*	
	if(tpm_gen_random(32, kk) == 0) {
		vfree(sm2para_k);
		return 0;
	}
	*/

	if(copy_from_user((unsigned int *)(&(sm2para_k->len)),(unsigned int *)(&(sm2para_u->len)),sizeof(int))){
		vfree(sm2para_k);
		return 0;
	}
	if(copy_from_user((unsigned char *)(sm2para_k->x),(unsigned char *)(sm2para_u->x),32)){
		vfree(sm2para_k);
		return 0;
	}
	if(copy_from_user((unsigned char *)(sm2para_k->y),(unsigned char *)(sm2para_u->y),32)){
		vfree(sm2para_k);
		return 0;
	}
	if(sm2para_k->len > (SM2_MAX_PLAIN_LEN)){
		vfree(sm2para_k);
		return 0;
	}
	if(copy_from_user((unsigned char *)(sm2para_k->plain),(unsigned char *)(sm2para_u->plain),sm2para_k->len)){
		vfree(sm2para_k);
		return 0;
	}
	CMpiInport(&x,sm2para_k->x,32);
	CMpiInport(&y,sm2para_k->y,32);
	SetPublicKey(&pk,&x,&y);
	rc = EncryptMessage(&pk,sm2para_k->cipher+1,sm2para_k->plain, sm2para_k->len, kk, 32);
	if(rc == 0)
	{
		vfree(sm2para_k);
		return 0;
	}
	sm2para_k->cipher[0] = 0x04;
	rc = rc + 1;
	if(copy_to_user(sm2para_u->cipher,sm2para_k->cipher,rc)) {
		vfree(sm2para_k);
		return 0;
	}
	if(copy_to_user((unsigned int *)(&(sm2para_u->len)),(unsigned int *)(&rc),sizeof(int))){
		vfree(sm2para_k);
		return 0;
	}
	vfree(sm2para_k);
	return 1;
}

/*sign the message out of TSX
* success: 1, fail :0
*/
int sm2_sign_test(SM2_SIGN_Para *sm2para_u){
	SM2_SIGN_Para *sm2para_k = NULL;
	CECCPrivateKey sk;
	int signRes,rc = 1;
	unsigned char kk[32] = {0x59,0x27,0x6E,0x27,0xD5,0x06,0x86,0x1A,0x16,0x68,0x0F,0x3A,0xD9,0xC0,0x2D,0xCC,0xEF,0x3C,0xC1,0xFA,0x3C,0xDB,0xE4,0xCE,0x6D,0x54,0xB8,0x0D,0xEA,0xC1,0xBC,0x21};
	//get_random_bytes(kk, 32);
	
	sm2para_k = vmalloc(sizeof(SM2_SIGN_Para));
	if(sm2para_k == NULL)
		return 0;

	if(copy_from_user((unsigned char*)(sm2para_k->pin), (unsigned char*)(sm2para_u->pin), PIN_LEN))
	{
		vfree(sm2para_k);
		return 0;
	}
	if(check_pin(sm2para_k->pin)==0)
	{
		vfree(sm2para_k);
		return 0;
	}
	/*
	if(tpm_gen_random(32, kk) == 0)
	{
		vfree(sm2para_k);
		return 0;
	}
	*/
	CMpiInit(&(sk.m_pntPx)); CMpiInit(&(sk.m_pntPy)); CMpiInit(&(sk.m_paramD));
	if(copy_from_user((unsigned int *)(&(sm2para_k->len)),(unsigned int *)(&(sm2para_u->len)),sizeof(int)))
	{
		vfree(sm2para_k);
		return 0;
	}
	if(sm2para_k->len > SM2_MAX_PLAIN_LEN)
	{
		vfree(sm2para_k);
		return 0;
	}
			
	if(copy_from_user((unsigned char *)(sm2para_k->d),(unsigned char *)(sm2para_u->d),32))
	{
		vfree(sm2para_k);
		return 0;
	}

	if(copy_from_user((unsigned char *)(sm2para_k->x),(unsigned char *)(sm2para_u->x),32))
	{
		vfree(sm2para_k);
		return 0;
	}

	if(copy_from_user((unsigned char *)(sm2para_k->y),(unsigned char *)(sm2para_u->y),32))
	{
		vfree(sm2para_k);
		return 0;
	}
			
	if(copy_from_user((unsigned char *)(sm2para_k->message),(unsigned char *)(sm2para_u->message),sm2para_k->len))
	{
		vfree(sm2para_k);
		return 0;
	}
	
	if(copy_from_user((unsigned char *)(&sm2para_k->LenOfpUserName),(unsigned char *)(&(sm2para_u->LenOfpUserName)),sizeof(int)))
	{
		vfree(sm2para_k);
		return 0;
	}
	
	if(sm2para_k->LenOfpUserName > SM2_MAX_PLAIN_LEN){
		vfree(sm2para_k);
		return 0;
	}

	if(copy_from_user((unsigned char *)(sm2para_k->pUserName),(unsigned char *)(sm2para_u->pUserName),sm2para_k->LenOfpUserName))
	{
		vfree(sm2para_k);
		return 0;
	}
	CMpiInport(&(sk.m_paramD), sm2para_k->d, 32);
	CMpiInport(&(sk.m_pntPy), sm2para_k->y, 32);
	CMpiInport(&(sk.m_pntPx), sm2para_k->x, 32);
	/*direct obtain the public key from the invoker				
	if(GenerateKey(&sk,sm2para_k->d,32) == 0)
	{
		vfree(sm2para_k);
		return 0;
	}*/
	signRes = SignMessage(&sk,sm2para_k->sign, sm2para_k->message, sm2para_k->len, sm2para_k->pUserName, sm2para_k->LenOfpUserName, kk, 32);
	if(signRes != 0)
	{
		if(copy_to_user(sm2para_u->sign,sm2para_k->sign,signRes))
			rc = 0;
		if(copy_to_user((unsigned int *)(&(sm2para_u->LenOfsign)),(unsigned int *)(&signRes),sizeof(int)))
			rc = 0;
	}
	else
		rc = 0;
	vfree(sm2para_k);
	return rc;
}


/******************************function out of TSX*************************************/
/*generate the key out of TSX
* success: 1, fail :0
*/
int sm2_normal_keygen(Gen_Key_Para *sm2keypara_u){
	CECCPrivateKey sk;
	int rc,i = 0;
	Gen_Key_Para *sm2keypara_k = NULL;

	sm2keypara_k = vmalloc(sizeof(Gen_Key_Para));
	if(sm2keypara_k == NULL)
		return 0;
	if(copy_from_user((unsigned char*)(sm2keypara_k->pin), (unsigned char*)(sm2keypara_u->pin), PIN_LEN))
	{
		vfree(sm2keypara_k);
		return 0;
	}
	if(check_pin(sm2keypara_k->pin)==0)
	{
		vfree(sm2keypara_k);
		return 0;
	}

	if(tpm_gen_random(32, sm2keypara_k->d) == 0)
	{
		vfree(sm2keypara_k);
		return 0;
	}
			
	CMpiInit(&(sk.m_pntPx)); CMpiInit(&(sk.m_pntPy)); CMpiInit(&(sk.m_paramD));
	
	for(i=0;i<12;i++)
		sk.empty_pad[i] = 0x0;

	if(GenerateKey(&sk,sm2keypara_k->d,32)!=1)
	{
		vfree(sm2keypara_k);
		return 0;
	}

	CMpiExport(&(sk.m_pntPx),sm2keypara_k->x, DCS_ECC_KEY_LENGTH);
	CMpiExport(&(sk.m_pntPy),sm2keypara_k->y, DCS_ECC_KEY_LENGTH);

	rc = 1;
	if(copy_to_user(sm2keypara_u->d, sm2keypara_k->d, 32))
		rc = 0;
	if(copy_to_user(sm2keypara_u->x, sm2keypara_k->x, 32))
		rc = 0;
	if(copy_to_user(sm2keypara_u->y, sm2keypara_k->y, 32))
		rc = 0;

	vfree(sm2keypara_k);
	return rc;
}

/*sign the message out of TSX
* success: 1, fail :0
*/
int sm2_normal_sign(SM2_SIGN_Para *sm2para_u){
	SM2_SIGN_Para *sm2para_k = NULL;
	CECCPrivateKey sk;
	int signRes,rc = 1;
	unsigned char kk[32] = {0x59,0x27,0x6E,0x27,0xD5,0x06,0x86,0x1A,0x16,0x68,0x0F,0x3A,0xD9,0xC0,0x2D,0xCC,0xEF,0x3C,0xC1,0xFA,0x3C,0xDB,0xE4,0xCE,0x6D,0x54,0xB8,0x0D,0xEA,0xC1,0xBC,0x21};
	//get_random_bytes(kk, 32);
	
	sm2para_k = vmalloc(sizeof(SM2_SIGN_Para));
	if(sm2para_k == NULL)
		return 0;

	if(copy_from_user((unsigned char*)(sm2para_k->pin), (unsigned char*)(sm2para_u->pin), PIN_LEN))
	{
		vfree(sm2para_k);
		return 0;
	}
	if(check_pin(sm2para_k->pin)==0)
	{
		vfree(sm2para_k);
		return 0;
	}
	if(tpm_gen_random(32, kk) == 0)
	{
		vfree(sm2para_k);
		return 0;
	}
	CMpiInit(&(sk.m_pntPx)); CMpiInit(&(sk.m_pntPy)); CMpiInit(&(sk.m_paramD));
	if(copy_from_user((unsigned int *)(&(sm2para_k->len)),(unsigned int *)(&(sm2para_u->len)),sizeof(int)))
	{
		vfree(sm2para_k);
		return 0;
	}
	if(sm2para_k->len > SM2_MAX_PLAIN_LEN)
	{
		vfree(sm2para_k);
		return 0;
	}
			
	if(copy_from_user((unsigned char *)(sm2para_k->d),(unsigned char *)(sm2para_u->d),32))
	{
		vfree(sm2para_k);
		return 0;
	}

	if(copy_from_user((unsigned char *)(sm2para_k->x),(unsigned char *)(sm2para_u->x),32))
	{
		vfree(sm2para_k);
		return 0;
	}

	if(copy_from_user((unsigned char *)(sm2para_k->y),(unsigned char *)(sm2para_u->y),32))
	{
		vfree(sm2para_k);
		return 0;
	}
			
	if(copy_from_user((unsigned char *)(sm2para_k->message),(unsigned char *)(sm2para_u->message),sm2para_k->len))
	{
		vfree(sm2para_k);
		return 0;
	}
	
	if(copy_from_user((unsigned char *)(&sm2para_k->LenOfpUserName),(unsigned char *)(&(sm2para_u->LenOfpUserName)),sizeof(int)))
	{
		vfree(sm2para_k);
		return 0;
	}
	
	if(sm2para_k->LenOfpUserName > SM2_MAX_PLAIN_LEN){
		vfree(sm2para_k);
		return 0;
	}

	if(copy_from_user((unsigned char *)(sm2para_k->pUserName),(unsigned char *)(sm2para_u->pUserName),sm2para_k->LenOfpUserName))
	{
		vfree(sm2para_k);
		return 0;
	}
	CMpiInport(&(sk.m_paramD), sm2para_k->d, 32);
	CMpiInport(&(sk.m_pntPy), sm2para_k->y, 32);
	CMpiInport(&(sk.m_pntPx), sm2para_k->x, 32);
	/*direct obtain the public key from the invoker				
	if(GenerateKey(&sk,sm2para_k->d,32) == 0)
	{
		vfree(sm2para_k);
		return 0;
	}*/
	signRes = SignMessage(&sk,sm2para_k->sign, sm2para_k->message, sm2para_k->len, sm2para_k->pUserName, sm2para_k->LenOfpUserName, kk, 32);
	if(signRes != 0)
	{
		if(copy_to_user(sm2para_u->sign,sm2para_k->sign,signRes))
			rc = 0;
		if(copy_to_user((unsigned int *)(&(sm2para_u->LenOfsign)),(unsigned int *)(&signRes),sizeof(int)))
			rc = 0;
	}
	else
		rc = 0;
	vfree(sm2para_k);
	return rc;
}

//SYX : need to check the point on curve?
int sm2_dec(SM2_Para *sm2para_u){
	CECCPrivateKey sk;
	SM2_Para *sm2para_k = NULL;
	int rc = 0;
	
	sm2para_k = vmalloc(sizeof(SM2_Para));
	if(sm2para_k == NULL)
		return 0;

	if(copy_from_user((unsigned char*)(sm2para_k->pin), (unsigned char*)(sm2para_u->pin), PIN_LEN)){
		vfree(sm2para_k);
		return 0;
	}
	if(check_pin(sm2para_k->pin)==0){
		vfree(sm2para_k);
		return 0;
	}
			
	CMpiInit(&(sk.m_pntPx)); CMpiInit(&(sk.m_pntPy)); CMpiInit(&(sk.m_paramD));
	if(copy_from_user((unsigned int *)(&(sm2para_k->len)),(unsigned int *)(&(sm2para_u->len)),sizeof(int))){
		vfree(sm2para_k);
		return 0;
	}
		
	if(copy_from_user((unsigned char *)(sm2para_k->d),(unsigned char *)(sm2para_u->d),32)){
		vfree(sm2para_k);
		return 0;
	}

	if(sm2para_k->len > (1+SM2_KEY_LEN*3+SM2_MAX_PLAIN_LEN)){
		vfree(sm2para_k);
		return 0;
	}
	
	if(copy_from_user((unsigned char *)(sm2para_k->cipher),(unsigned char *)(sm2para_u->cipher),sm2para_k->len)){
		vfree(sm2para_k);
		return 0;
	}
	CMpiInport(&(sk.m_paramD), sm2para_k->d, 32);		
	//if(GenerateKey(&sk,sm2para_k->d,32) != 0)
	{
		rc = DecryptMessage(&sk,sm2para_k->plain,(sm2para_k->cipher)+1,sm2para_k->len-1);
	}
	if(rc != 0)
	{
		if(copy_to_user(sm2para_u->plain,sm2para_k->plain,rc)){
			vfree(sm2para_k);
			return 0;
		}
		if(copy_to_user((unsigned int *)(&(sm2para_u->len)),(unsigned int *)(&rc),sizeof(int))){
			vfree(sm2para_k);
			return 0;
		}
	}
	vfree(sm2para_k);
	return rc;
}

/******************************for test*************************************/
int sm2_verify(SM2_SIGN_Para *sm2para_u){
	CECCPublicKey pk;
	CMpi x,y;
	SM2_SIGN_Para *sm2para_k = NULL;
	int rc;
	sm2para_k = vmalloc(sizeof(SM2_SIGN_Para));
	if(sm2para_k == NULL){
		return 0;
	}
	if(copy_from_user((unsigned char*)(sm2para_k->pin), (unsigned char*)(sm2para_u->pin), PIN_LEN)){
		vfree(sm2para_k);
		return 0;
	}
			
	if(check_pin(sm2para_k->pin)==0){
		vfree(sm2para_k);
		return 0;
	}
			
	if(copy_from_user((unsigned int *)(&(sm2para_k->len)),(unsigned int *)(&(sm2para_u->len)),sizeof(int))){
		vfree(sm2para_k);
		return 0;
	}

	if(sm2para_k->len > SM2_MAX_PLAIN_LEN)
	{
		vfree(sm2para_k);
		return 0;
	}
	
	if(copy_from_user((unsigned char *)(sm2para_k->x),(unsigned char *)(sm2para_u->x),32)){
		vfree(sm2para_k);
		return 0;
	}

	if(copy_from_user((unsigned char *)(sm2para_k->y),(unsigned char *)(sm2para_u->y),32)){
		vfree(sm2para_k);
		return 0;
	}
	
	if(copy_from_user((unsigned char *)(sm2para_k->message),(unsigned char *)(sm2para_u->message),sm2para_k->len)){
		vfree(sm2para_k);
		return 0;
	}
	if(copy_from_user((unsigned int *)(&(sm2para_k->LenOfsign)),(unsigned int *)(&(sm2para_u->LenOfsign)),sizeof(int))){
		vfree(sm2para_k);
		return 0;
	}

	if(sm2para_k->LenOfsign >(SM2_KEY_LEN*2 + 1)){
		vfree(sm2para_k);
		return 0;
	}

	if(copy_from_user((unsigned char *)(sm2para_k->sign),(unsigned char *)(sm2para_u->sign),sm2para_k->LenOfsign)){
		vfree(sm2para_k);
		return 0;
	}		
	if(copy_from_user((unsigned char *)(&sm2para_k->LenOfpUserName),(unsigned char *)(&(sm2para_u->LenOfpUserName)),sizeof(int))){
		vfree(sm2para_k);
		return 0;
	}
	if(sm2para_k->LenOfpUserName > SM2_MAX_PLAIN_LEN){
		vfree(sm2para_k);
		return 0;
	}
	
	if(copy_from_user((unsigned char *)(sm2para_k->pUserName),(unsigned char *)(sm2para_u->pUserName),sm2para_k->LenOfpUserName)){
		vfree(sm2para_k);
		return 0;
	}
		
	CMpiInport(&x,sm2para_k->x,32);
	CMpiInport(&y,sm2para_k->y,32);
	SetPublicKey(&pk,&x,&y);

	rc = VerifyMessage(&pk,sm2para_k->message, sm2para_k->len,sm2para_k->sign,sm2para_k->LenOfsign,sm2para_k->pUserName, sm2para_k->LenOfpUserName);
	vfree(sm2para_k);
	return rc;
}
int sm2_enc(SM2_Para *sm2para_u){
	int rc;
	CECCPublicKey pk;
	CMpi x,y;	
	SM2_Para *sm2para_k = NULL;		
	unsigned char kk[32] = {0x59,0x27,0x6E,0x27,0xD5,0x06,0x86,0x1A,0x16,0x68,0x0F,0x3A,0xD9,0xC0,0x2D,0xCC,0xEF,0x3C,0xC1,0xFA,0x3C,0xDB,0xE4,0xCE,0x6D,0x54,0xB8,0x0D,0xEA,0xC1,0xBC,0x21};
	//get_random_bytes(kk, 32);

	sm2para_k = vmalloc(sizeof(SM2_Para));
	if(sm2para_k == NULL)
		return 0;

	if(copy_from_user((unsigned char*)(sm2para_k->pin), (unsigned char*)(sm2para_u->pin), PIN_LEN)){
		vfree(sm2para_k);
		return 0;
	}
	if(check_pin(sm2para_k->pin)==0) {
		vfree(sm2para_k);
		return 0;
	}
		
	if(tpm_gen_random(32, kk) == 0) {
		vfree(sm2para_k);
		return 0;
	}

	if(copy_from_user((unsigned int *)(&(sm2para_k->len)),(unsigned int *)(&(sm2para_u->len)),sizeof(int))){
		vfree(sm2para_k);
		return 0;
	}
	if(copy_from_user((unsigned char *)(sm2para_k->x),(unsigned char *)(sm2para_u->x),32)){
		vfree(sm2para_k);
		return 0;
	}
	if(copy_from_user((unsigned char *)(sm2para_k->y),(unsigned char *)(sm2para_u->y),32)){
		vfree(sm2para_k);
		return 0;
	}
	if(sm2para_k->len > (SM2_MAX_PLAIN_LEN)){
		vfree(sm2para_k);
		return 0;
	}
	if(copy_from_user((unsigned char *)(sm2para_k->plain),(unsigned char *)(sm2para_u->plain),sm2para_k->len)){
		vfree(sm2para_k);
		return 0;
	}

#ifdef PRINT_DBG
	printk( " SM2_Para: \n");
	printhex((unsigned char *)sm2para_k, sizeof(SM2_Para));
#endif

	CMpiInport(&x,sm2para_k->x,32);
	CMpiInport(&y,sm2para_k->y,32);
	SetPublicKey(&pk,&x,&y);
	rc = EncryptMessage(&pk,sm2para_k->cipher+1,sm2para_k->plain, sm2para_k->len, kk, 32);
	if(rc == 0)
	{
		vfree(sm2para_k);
		return 0;
	}
	sm2para_k->cipher[0] = 0x04;
	rc = rc + 1;
	if(copy_to_user(sm2para_u->cipher,sm2para_k->cipher,rc)) {
		vfree(sm2para_k);
		return 0;
	}
	if(copy_to_user((unsigned int *)(&(sm2para_u->len)),(unsigned int *)(&rc),sizeof(int))){
		vfree(sm2para_k);
		return 0;
	}
	vfree(sm2para_k);
	return 1;
}



/******************************function in TSX*************************************/
/*
* the SM2 Key Generate is executed in TSX
* success: 1, fail :0
*/
int sm2_safe_keygen(Gen_Key_Para *sm2keypara_u){
	CECCPrivateKey sk;
	Gen_Key_Para sm2keypara_k; 
	int i,rc;
	//SYX: code warm for what?
	unsigned char temp[16] = {0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0}; //temp var for warm


	if(copy_from_user((unsigned char*)(sm2keypara_k.pin), (unsigned char*)(sm2keypara_u->pin), PIN_LEN))
	{
		return 0;
	}	
	if(check_pin(sm2keypara_k.pin)==0)
	{
		return 0;
	}	

	if(tpm_gen_random(32, sm2keypara_k.d) == 0)
	{
		return 0;
	}	
			
	CMpiInit(&(sk.m_pntPx)); CMpiInit(&(sk.m_pntPy)); CMpiInit(&(sk.m_paramD));
	
	for(i=0;i<12;i++)
		sk.empty_pad[i] = 0x0;

	//code warm 

	SM4DecryptWithMode(temp, 16, temp, 16, NULL, ECB, NULL);
	SM4EncryptWithMode(temp, 16, temp, 16, NULL, ECB, NULL);
	if(GenerateKeySafe(&sk,sm2keypara_k.d,32)!=1)
	{
		return 0;
	}
		
	rc = 1;
	// SM4DecryptWithMode((BYTE *)&(sk.m_pntPx), sizeof(CMpi), (BYTE *)&(sk.m_pntPx), sizeof(CMpi), NULL, ECB, NULL);
	// SM4DecryptWithMode((BYTE *)&(sk.m_pntPy), sizeof(CMpi), (BYTE *)&(sk.m_pntPy), sizeof(CMpi), NULL, ECB, NULL);
	CMpiExport(&(sk.m_pntPx),sm2keypara_k.x, DCS_ECC_KEY_LENGTH);
	CMpiExport(&(sk.m_pntPy),sm2keypara_k.y, DCS_ECC_KEY_LENGTH);

	if(copy_to_user(sm2keypara_u->d, sm2keypara_k.d, 32))
		rc = 0;
	if(copy_to_user(sm2keypara_u->x, sm2keypara_k.x, 32))
		rc = 0;
	if(copy_to_user(sm2keypara_u->y, sm2keypara_k.y, 32))
		rc = 0; 
	return rc;
}

/*sign the message in the TSX
* success: 1, fail :0
*/
int sm2_safe_sign(SM2_SIGN_Para *sm2para_u){
	CECCPrivateKey sk;
	SM2_SIGN_Para sm2para_k;
	int rc = 0;

	unsigned char temp[16] = {0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0};
	unsigned char kk[32] = {0x59,0x27,0x6E,0x27,0xD5,0x06,0x86,0x1A,0x16,0x68,0x0F,0x3A,0xD9,0xC0,0x2D,0xCC,0xEF,0x3C,0xC1,0xFA,0x3C,0xDB,0xE4,0xCE,0x6D,0x54,0xB8,0x0D,0xEA,0xC1,0xBC,0x21};

	if(copy_from_user((unsigned char*)(sm2para_k.pin), (unsigned char*)(sm2para_u->pin), PIN_LEN)){
		return 0;
	}
			
	if(check_pin(sm2para_k.pin)==0){
		return 0;
	}
	
	if(tpm_gen_random(32, kk) == 0){
		return 0;
	}


	if(copy_from_user((unsigned int *)(&(sm2para_k.len)),(unsigned int *)(&(sm2para_u->len)),sizeof(int))){
		return 0;
	}

	if(sm2para_k.len > SM2_MAX_PLAIN_LEN)
		return 0;
			
	if(copy_from_user((unsigned char *)(sm2para_k.d),(unsigned char *)(sm2para_u->d),32)){
		return 0;
	}
	if(copy_from_user((unsigned char *)(sm2para_k.x),(unsigned char *)(sm2para_u->x),32)){
		return 0;
	}
	if(copy_from_user((unsigned char *)(sm2para_k.y),(unsigned char *)(sm2para_u->y),32)){
		return 0;
	}

	if(copy_from_user((unsigned char *)(sm2para_k.message),(unsigned char *)(sm2para_u->message),sm2para_k.len)){
		return 0;
	}
			
	if(copy_from_user((unsigned char *)(&sm2para_k.LenOfpUserName),(unsigned char *)(&(sm2para_u->LenOfpUserName)),sizeof(int))){
		return 0;
	}

	if(sm2para_k.LenOfpUserName > SM2_MAX_PLAIN_LEN)
		return 0;
	
	if(copy_from_user((unsigned char *)(sm2para_k.pUserName),(unsigned char *)(sm2para_u->pUserName),sm2para_k.LenOfpUserName)){
		return 0;
	}
			
			
	CMpiInit(&(sk.m_pntPx)); CMpiInit(&(sk.m_pntPy)); CMpiInit(&(sk.m_paramD));
	memset(sk.empty_pad,0x0,12);
	
	//code warm
	SM4DecryptWithMode(temp, 16, temp, 16, NULL, ECB, NULL);
	SM4EncryptWithMode(temp, 16, temp, 16, NULL, ECB, NULL);


	/* the public key is tansmit through the invoker, we donot need to invoke the generate key again*/
	/*if(GenerateKeySafe(&sk,sm2para_k->d,32)!=1)
	{
		vfree(sm2para_k);
		return 0;
	}*/

	rc = ImportSM2Secert(sm2para_k.d, &(sk.m_paramD), NULL);
	if(rc <= 0)
		return rc;

	CMpiInport(&(sk.m_pntPy), sm2para_k.y, 32);
	CMpiInport(&(sk.m_pntPx), sm2para_k.x, 32);

	rc = SignMessageSafe(&sk,sm2para_k.sign, sm2para_k.message, sm2para_k.len, sm2para_k.pUserName, sm2para_k.LenOfpUserName, kk, 32);
	if(rc <= 0)
	{
		return rc;
	}	
	
	if(copy_to_user(sm2para_u->sign,sm2para_k.sign,rc))
		rc = 0;	
	
	if(copy_to_user((unsigned int *)(&(sm2para_u->LenOfsign)),(unsigned int *)(&rc),sizeof(int)))
		rc = 0;
	return rc;
}

//SYX : need to check the point on curve?
// SYX : final msg encrypted by SM4, is it nessasery? what's for sm4 dec method 
int sm2_safe_dec(SM2_Para *sm2para_u){
	CECCPrivateKey sk;

	int rc = 0;//the time to try to start the transaction
	SM2_Para sm2para_k;
	unsigned char temp[16] = {0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0};
	
	if(copy_from_user((unsigned char*)(sm2para_k.pin), (unsigned char*)(sm2para_u->pin), PIN_LEN)){
		return 0;
	}
	if(check_pin(sm2para_k.pin)==0){
		return 0;	 
	}		
	if(copy_from_user((unsigned int *)(&(sm2para_k.len)),(unsigned int *)(&(sm2para_u->len)),sizeof(int))){
		return 0;
	}
	if(copy_from_user((unsigned char *)(sm2para_k.d),(unsigned char *)(sm2para_u->d),SM2_KEY_LEN)){
		return 0;
	}

	if(sm2para_k.len >(1+SM2_KEY_LEN*3+SM2_MAX_PLAIN_LEN))
		return 0;

	if(copy_from_user((unsigned char *)(sm2para_k.cipher),(unsigned char *)(sm2para_u->cipher),sm2para_k.len)){
		return 0;
	}

	CMpiInit(&(sk.m_pntPx)); CMpiInit(&(sk.m_pntPy)); CMpiInit(&(sk.m_paramD));
	memset(sk.empty_pad,0x0,12);

	SM4DecryptWithMode(temp, 16, temp, 16, NULL, ECB, NULL);
	SM4EncryptWithMode(temp, 16, temp, 16, NULL, ECB, NULL);

	rc = ImportSM2Secert(sm2para_k.d, &(sk.m_paramD), NULL);
	if(rc <= 0)
		return rc;

	rc = DecryptMessageSafe(&sk,sm2para_k.plain,sm2para_k.cipher+1,sm2para_k.len-1);
	if(rc == 0){
		return 0;
	}
	if(copy_to_user(sm2para_u->plain,sm2para_k.plain,rc))
		rc = 0;
	if(copy_to_user((unsigned int *)(&(sm2para_u->len)),(unsigned int *)(&rc),sizeof(int)))
		rc = 0;
	return rc;
}

int hmac_init(GM_HMAC *gm_hmac_u){
	GM_HMAC * gm_hmac_k;
	// unsigned char buff[GM_HMAC_MD_CBLOCK_SIZE];
	unsigned char temp[16] = {0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0};
    int i, ret;
#ifdef TSX_ENABLE
	int tsxflag = 0;
	int status,try = 0;
	unsigned long flags;
#endif
	gm_hmac_k = vmalloc(sizeof(GM_HMAC));
	if(copy_from_user((unsigned char *)(gm_hmac_k->pin),(unsigned char *)(gm_hmac_u->pin),PIN_LEN))
	{
		ret = 0;
		goto err;
	}
	if(check_pin(gm_hmac_k->pin)==0)
	{
		ret = 0;
		goto err;
	}
	if(copy_from_user(gm_hmac_k, gm_hmac_u, sizeof(GM_HMAC)))
	{
		ret = 0;
		goto err;
	}
	SM4DecryptWithMode(temp, 16, temp, 16, NULL, ECB, NULL);
	SM4EncryptWithMode(temp, 16, temp, 16, NULL, ECB, NULL);

#ifdef TSX_ENABLE
	tsx_header("hmac_init k import", 1, tsxflag, flags, try, status);
#endif
	if(SM4DecryptWithMode(gm_hmac_k->key, gm_hmac_k->klen, gm_hmac_k->plain, gm_hmac_k->klen, NULL, ECB, NULL)==0)
	{
#ifdef TSX_ENABLE
		tsx_error_tailer(flags)
#endif	
		ret = 0;
		goto err;
	}
	memset(gm_hmac_k->plain + gm_hmac_k->klen, 0, GM_HMAC_MD_CBLOCK_SIZE-gm_hmac_k->klen);
	for (i = 0; i < GM_HMAC_MD_CBLOCK_SIZE; i++)
		gm_hmac_k->plain[i] ^= 0x36;
	Sm3HashInit((SM3_HASH_STATE *)&(gm_hmac_k->state), gm_hmac_k->plain, GM_HMAC_MD_CBLOCK_SIZE);
	memset(gm_hmac_k->plain , 0, sizeof(gm_hmac_k->plain));
	if(SM4EncryptWithMode((unsigned char*)&(gm_hmac_k->state), sizeof(gm_hmac_k->state), (unsigned char*)&(gm_hmac_k->state), sizeof(gm_hmac_k->state), NULL, ECB, NULL)==0){
#ifdef TSX_ENABLE
		tsx_error_tailer(flags)
#endif	
		ret = 0;
		goto err;		
	}
#ifdef TSX_ENABLE
	tsx_normal_tailer(tsxflag, flags)
#endif

	if(copy_to_user(gm_hmac_u, gm_hmac_k, sizeof(GM_HMAC))){
		ret = 0;
		goto err;
	}
	ret = 1;
err:
	vfree(gm_hmac_k);
	return ret;
}

int hmac_update(GM_HMAC * gm_hmac_u){
	GM_HMAC * gm_hmac_k;
	// unsigned char buff[GM_HMAC_MD_CBLOCK_SIZE];
	unsigned char temp[16] = {0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0};
    int ret;
#ifdef TSX_ENABLE
	int tsxflag = 0;
	int status,try = 0;
	unsigned long flags;
#endif
	gm_hmac_k = vmalloc(sizeof(GM_HMAC));
	if(copy_from_user((unsigned char *)(gm_hmac_k->pin),(unsigned char *)(gm_hmac_u->pin),PIN_LEN))
	{
		ret = 0;
		goto err;
	}
	if(check_pin(gm_hmac_k->pin)==0)
	{
		ret = 0;
		goto err;
	}
	if(copy_from_user(gm_hmac_k, gm_hmac_u, sizeof(GM_HMAC)))
	{
		ret = 0;
		goto err;
	}

	if(gm_hmac_k->plainlen > MAX_PLAIN_LEN)
	{
		ret = 0;
		goto err;
	}
	SM4DecryptWithMode(temp, 16, temp, 16, NULL, ECB, NULL);
	SM4EncryptWithMode(temp, 16, temp, 16, NULL, ECB, NULL);

#ifdef TSX_ENABLE
	tsx_header("hmac_update", 1, tsxflag, flags, try, status);
#endif
	if(SM4DecryptWithMode((unsigned char*)&(gm_hmac_k->state), sizeof(gm_hmac_k->state), (unsigned char*)&(gm_hmac_k->state), sizeof(gm_hmac_k->state), NULL, ECB, NULL)==0)
	{
#ifdef TSX_ENABLE
		tsx_error_tailer(flags)
#endif	
		ret = 0;
		goto err;
	}
	Sm3HashPending((SM3_HASH_STATE *)&(gm_hmac_k->state), gm_hmac_k->plain, gm_hmac_k->plainlen);
	if(SM4EncryptWithMode((unsigned char*)&(gm_hmac_k->state), sizeof(gm_hmac_k->state), (unsigned char*)&(gm_hmac_k->state), sizeof(gm_hmac_k->state), NULL, ECB, NULL)==0){
#ifdef TSX_ENABLE
		tsx_error_tailer(flags)
#endif	
		ret = 0;
		goto err;		
	}
#ifdef TSX_ENABLE
	tsx_normal_tailer(tsxflag, flags)
#endif

	if(copy_to_user(gm_hmac_u, gm_hmac_k, sizeof(GM_HMAC))){
		ret = 0;
		goto err;
	}
	ret = 1;
err:
	vfree(gm_hmac_k);
	return ret;
}

int hmac_final(GM_HMAC * gm_hmac_u){
	GM_HMAC * gm_hmac_k;
	// unsigned char buff[GM_HMAC_MD_CBLOCK_SIZE];
	unsigned char temp[16] = {0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0};
	unsigned char digest[SM3_DIGEST_LEN];
    int i, ret;
#ifdef TSX_ENABLE
	int tsxflag = 0;
	int status,try = 0;
	unsigned long flags;
#endif
	gm_hmac_k = vmalloc(sizeof(GM_HMAC));
	if(copy_from_user((unsigned char *)(gm_hmac_k->pin),(unsigned char *)(gm_hmac_u->pin),PIN_LEN))
	{
		ret = 0;
		goto err;
	}
	if(check_pin(gm_hmac_k->pin)==0)
	{
		ret = 0;
		goto err;
	}
	if(copy_from_user(gm_hmac_k, gm_hmac_u, sizeof(GM_HMAC)))
	{
		ret = 0;
		goto err;
	}

	if(gm_hmac_k->plainlen > MAX_PLAIN_LEN)
	{
		ret = 0;
		goto err;
	}
	SM4DecryptWithMode(temp, 16, temp, 16, NULL, ECB, NULL);
	SM4EncryptWithMode(temp, 16, temp, 16, NULL, ECB, NULL);

#ifdef TSX_ENABLE
	tsx_header("hmac_final last msg", 1, tsxflag, flags, try, status);
#endif
	if(SM4DecryptWithMode((unsigned char*)&(gm_hmac_k->state), sizeof(gm_hmac_k->state), (unsigned char*)&(gm_hmac_k->state), sizeof(gm_hmac_k->state), NULL, ECB, NULL)==0)
	{
#ifdef TSX_ENABLE
		tsx_error_tailer(flags)
#endif	
		ret = 0;
		goto err;
	}
	if(gm_hmac_k->plainlen)
		Sm3HashPending((SM3_HASH_STATE *)&(gm_hmac_k->state), gm_hmac_k->plain, gm_hmac_k->plainlen);
	Sm3HashFinal(digest, (SM3_HASH_STATE *)&(gm_hmac_k->state));

	memset(&(gm_hmac_k->state), 0, sizeof(gm_hmac_k->state));
	if(SM4EncryptWithMode(digest, sizeof(digest), (unsigned char*)&(digest), sizeof(digest), NULL, ECB, NULL)==0){
#ifdef TSX_ENABLE
		tsx_error_tailer(flags)
#endif	
		ret = 0;
		goto err;		
	}
#ifdef TSX_ENABLE
	tsx_normal_tailer(tsxflag, flags)
#endif

#ifdef TSX_ENABLE
	tsx_header("hmac_final outter", 1, tsxflag, flags, try, status);
#endif
	if((SM4DecryptWithMode(gm_hmac_k->key, gm_hmac_k->klen, gm_hmac_k->plain, gm_hmac_k->klen, NULL, ECB, NULL)==0 )
		|| (SM4DecryptWithMode(digest, sizeof(digest), (unsigned char*)&(digest), sizeof(digest), NULL, ECB, NULL)==0 ) )
	{
#ifdef TSX_ENABLE
		tsx_error_tailer(flags)
#endif	
		ret = 0;
		goto err;
	}
	memset(gm_hmac_k->plain + gm_hmac_k->klen, 0, GM_HMAC_MD_CBLOCK_SIZE-gm_hmac_k->klen);
	for (i = 0; i < GM_HMAC_MD_CBLOCK_SIZE; i++)
		gm_hmac_k->plain[i] ^= 0x5c;
	Sm3HashInit((SM3_HASH_STATE *)&(gm_hmac_k->state), gm_hmac_k->plain, GM_HMAC_MD_CBLOCK_SIZE);
	Sm3HashPending((SM3_HASH_STATE *)&(gm_hmac_k->state), digest, sizeof(digest));
	Sm3HashFinal(gm_hmac_k->digest, (SM3_HASH_STATE *)&(gm_hmac_k->state));

	memset(gm_hmac_k->plain , 0, sizeof(gm_hmac_k->plain));
	memset(digest , 0, sizeof(digest));
	memset(&(gm_hmac_k->state), 0, sizeof(gm_hmac_k->state));

#ifdef TSX_ENABLE
	tsx_normal_tailer(tsxflag, flags)
#endif

	if(copy_to_user(gm_hmac_u, gm_hmac_k, sizeof(GM_HMAC))){
		ret = 0;
		goto err;
	}
	ret = 1;
err:
	vfree(gm_hmac_k);
	return ret;
}


int sm4_op(SM4_Para *sm4para_u){
	SM4_Para *sm4para_k =NULL; 
	unsigned char temp[16] = {0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0};
#ifdef TSX_ENABLE
	int tsxflag = 0;
	int status,try = 0;
	unsigned long flags;
#endif

	int rc, len, lastlen;
	sm4para_k = vmalloc(sizeof(SM4_Para) + MAX_PLAIN_LEN);
	if(sm4para_k == NULL)
		return 0;

	if(copy_from_user((unsigned char *)(sm4para_k->pin),(unsigned char *)(sm4para_u->pin),PIN_LEN)){
		rc = 0;
		goto err;
	}
	if(check_pin(sm4para_k->pin)==0){
		rc = 0;
		goto err;
	}
	
	if(copy_from_user(sm4para_k, sm4para_u,sizeof(SM4_Para))){
		rc = 0;
		goto err;
	}

	if(sm4para_k->lastlen > SM4_KEY_LEN || sm4para_k->len > MAX_PLAIN_LEN)
	{
		rc = 0;
		goto err;
	}
	len = sm4para_k->lastlen + sm4para_k->len;
	lastlen = (len/16)*16;

	if(sm4para_k->flag == 0){	//decrypt
		if(len == lastlen)
			lastlen -=16;	//if no remain, hold a block
		memcpy(sm4para_k->last + sm4para_k->lastlen, sm4para_k->cipher, sm4para_k->len);
	}
	else{	//encrypt
		memcpy(sm4para_k->last + sm4para_k->lastlen, sm4para_k->plain, sm4para_k->len);
	}

	SM4DecryptWithMode(temp, 16, temp, 16, NULL, ECB, NULL);
	SM4EncryptWithMode(temp, 16, temp, 16, NULL, ECB, NULL);

	if(lastlen)
	{
		if(sm4para_k->flag == 1 && sm4para_k->mode==2 && !sm4para_k->hasiv)	//encrypt cbc
		{
			tpm_gen_random(16, sm4para_k->iv);
			memcpy(sm4para_k->iniv,sm4para_k->iv,16);
			sm4para_k->hasiv = 1;
		}
#ifdef TSX_ENABLE
		tsx_header("sm4_op", 1, tsxflag, flags, try, status)
#endif
		
		if(SM4DecryptWithMode((unsigned char*)sm4para_k->key, SM4_KEY_LEN,(unsigned char*)temp, SM4_KEY_LEN, NULL, ECB, NULL)==0)
		{
#ifdef TSX_ENABLE
			tsx_error_tailer(flags);
#endif
			rc = 0;
			goto err;
		}
					
		if(sm4para_k->flag == 0){//decrypt
			rc = SM4DecryptWithMode(sm4para_k->last, lastlen, sm4para_k->plain, sm4para_k->hasiv , sm4para_k->iv, sm4para_k->mode, temp);
			// printk("%d, %d, %s\n", rc, lastlen, sm4para_k->plain);
		}
		else{//enc
			rc = SM4EncryptWithMode(sm4para_k->last, lastlen, sm4para_k->cipher, sm4para_k->hasiv, sm4para_k->iv, sm4para_k->mode, temp);
		}

		memset(temp, 0, 16);

#ifdef TSX_ENABLE
		tsx_normal_tailer(tsxflag, flags);
#endif
	}

	sm4para_k->len = rc;
	sm4para_k->lastlen = len - lastlen;
	if(lastlen)		//processed some block
	{
		if(sm4para_k->mode==2)	//update iv
		{
			if(sm4para_k->flag == 1)	//enc
				memcpy(sm4para_k->iv, sm4para_k->cipher+lastlen-16, 16);
			else
				memcpy(sm4para_k->iv, sm4para_k->last+lastlen-16, 16);
			sm4para_k->hasiv = 1;
		}
		if(sm4para_k->lastlen)	//update last
		{
			memcpy(sm4para_k->last, sm4para_k->last+lastlen, sm4para_k->lastlen);
		}
	}

	if(copy_to_user(sm4para_u, sm4para_k, sizeof(SM4_Para)))
	{
		rc = 0;
		goto err;
	}
	rc = 1;
	
err:
	vfree(sm4para_k);
	return rc;
}

int sm4_op_pad(SM4_Para *sm4para_u){
	SM4_Para *sm4para_k =NULL; 
	unsigned char temp[16] = {0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0};
#ifdef TSX_ENABLE
	int tsxflag = 0;
	int status,try = 0;
	unsigned long flags;
#endif

	int rc, len;
	sm4para_k = vmalloc(sizeof(SM4_Para) + MAX_PLAIN_LEN);
	if(sm4para_k == NULL)
		return 0;

	if(copy_from_user((unsigned char *)(sm4para_k->pin),(unsigned char *)(sm4para_u->pin),PIN_LEN)){
		rc = 0;
		goto err;
	}
	if(check_pin(sm4para_k->pin)==0){
		rc = 0;
		goto err;
	}
	
	if(copy_from_user(sm4para_k, sm4para_u,sizeof(SM4_Para))){
		rc = 0;
		goto err;
	}

	if(sm4para_k->lastlen > SM4_KEY_LEN || sm4para_k->len > MAX_PLAIN_LEN)
	{
		rc = 0;
		goto err;
	}
	len = sm4para_k->lastlen + sm4para_k->len;

	if(sm4para_k->flag == 0){	//decrypt
		memcpy(sm4para_k->last + sm4para_k->lastlen, sm4para_k->cipher, sm4para_k->len);
	}
	else{	//encrypt
		memcpy(sm4para_k->last + sm4para_k->lastlen, sm4para_k->plain, sm4para_k->len);
	}

	SM4DecryptWithMode(temp, 16, temp, 16, NULL, ECB, NULL);
	SM4EncryptWithMode(temp, 16, temp, 16, NULL, ECB, NULL);


	if(sm4para_k->flag == 1 && sm4para_k->mode==2 && !sm4para_k->hasiv)	//encrypt cbc
	{
		tpm_gen_random(16, sm4para_k->iv);
		memcpy(sm4para_k->iniv,sm4para_k->iv,16);
	}
	if(sm4para_k->flag == 0 && (len & (SMS4_BLOCK_LENGTH - 1)))	//decrypt but cipher len problem
	{
		rc = 0;
		goto err;
	}
#ifdef TSX_ENABLE
	tsx_header("sm4_op_pad", 1, tsxflag, flags, try, status)
#endif
	
	if(SM4DecryptWithMode((unsigned char*)sm4para_k->key, SM4_KEY_LEN,(unsigned char*)temp, SM4_KEY_LEN, NULL, ECB, NULL)==0)
	{
#ifdef TSX_ENABLE
		tsx_error_tailer(flags);
#endif
		rc = 0;
		goto err;
	}
				
	if(sm4para_k->flag == 0){//decrypt
		rc = SM4DecryptWithModePad(sm4para_k->last, len, sm4para_k->plain, sm4para_k->hasiv , sm4para_k->iv, sm4para_k->mode, temp);
	}
	else{//enc
		rc = SM4EncryptWithModePad(sm4para_k->last, len, sm4para_k->cipher, sm4para_k->hasiv, sm4para_k->iv, sm4para_k->mode, temp);
	}

	memset(temp, 0, 16);

#ifdef TSX_ENABLE
	tsx_normal_tailer(tsxflag, flags);
#endif

	sm4para_k->len = rc;
	sm4para_k->lastlen = 0;

	if(copy_to_user(sm4para_u, sm4para_k, sizeof(SM4_Para)))
	{
		rc = 0;
		goto err;
	}
	rc = 1;
	
err:
	vfree(sm4para_k);
	return rc;
}

// SYX : in kernel, maybe we dont need to provide a total crypto service(sign, enc , genkey ...),
// instead we can provide compute service (kernel compute k*G, d*P safely for user, user can interaction with kernel to assemble a crypto method) 
// however, some malicious user can use this to recover private key (use same k to sign)
// so if we use this module, kernel need to provide CCA security using key in register when interaction with user

static long template_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	if(IntegrityVerifed != 1)
		goto err;
	if((cmd!=INIT && cmd!=SELF_TEST) &&(ServiceAvailable!=1 && Inited !=1))
	{
		WriteLogFile("The module has not been inited or not ready for service\n");
		goto err;
	}	
	switch(cmd){
		case INIT:{ // init the sm4 key to register dr0,dr1 in each cpu, to expose only crypto message out of cache
			unsigned char key[MASTER_KEY_SIZE];
			unsigned char pin[PIN_LEN+SALT_LEN];
			INIT_Para *initarg_u = (INIT_Para *)arg;
			memset(key,0,MASTER_KEY_SIZE);
			if(copy_from_user(key, initarg_u->masterKey, MASTER_KEY_SIZE)){
			    printk("Error: Copy master key from user to kernel\n");
				WriteLogFile("Error: Copy master key from user to kernel\n");
				goto err;
			}
			if(init_hsm(key) != 1)
				goto err;
			if(copy_from_user(pin, initarg_u->pin, PIN_LEN)){
			    printk("Error: Copy PIN from user to kernel\n");
				WriteLogFile("Error: Copy PIN from user to kernel\n");
				goto err;
			}
			memcpy(pin+PIN_LEN,salt,SALT_LEN);
			Sm3Hash(pin_list, pin,PIN_LEN+SALT_LEN);	
			break;
		}
		
		case SELF_TEST:{
			if(self_test() != 1){
				ServiceAvailable = 0;
				goto err;
			}	
			break;
		}
		
		case SM2_SAFE_KEYGEN:{
			Gen_Key_Para *sm2keypara_u = (Gen_Key_Para *)arg;
			if(sm2_safe_keygen(sm2keypara_u)!=1)
				goto err;
			break;	
		}

		case SM2_KEYGEN:{
			Gen_Key_Para *sm2keypara_u = (Gen_Key_Para *)arg;
			if(sm2_normal_keygen(sm2keypara_u)!=1)
				goto err;
				
			break;	
		}
		
		case SM2_SIGN_TEST:{
			SM2_SIGN_Para *sm2para_u = (SM2_SIGN_Para *)arg;
			if(sm2_sign_test(sm2para_u)!=1)
				goto err;				
			break;	
		}

		case SM2_SIGN:{
			SM2_SIGN_Para *sm2para_u = (SM2_SIGN_Para *)arg;
			if(sm2_normal_sign(sm2para_u)!=1)
				goto err;				
			break;	
		}

		case SM2_SAFE_SIGN:{
			SM2_SIGN_Para *sm2para_u = (SM2_SIGN_Para *)arg;
			if(sm2_safe_sign(sm2para_u) <= 0)
				goto err;				
			break;		
		}
		case SM2_VERIFY:{
			SM2_SIGN_Para *sm2para_u = (SM2_SIGN_Para *)arg;
			if(sm2_verify(sm2para_u) == 1)
				return 1; //1 correct, 0 error			
			break;	

		}
		case SM2_DEC:{		
			SM2_Para *sm2para_u = (SM2_Para *)arg;
			if(sm2_dec(sm2para_u)==0)
				goto err;
			break;
		}//SM2 Dec
		case SM2_SAFE_DEC:{
			SM2_Para *sm2para_u = (SM2_Para *)arg;
			if(sm2_safe_dec(sm2para_u)<=0)
				goto err;
			break;
		}
		case SM2_ENC:{
			SM2_Para *sm2para_u = (SM2_Para *)arg;
			if(sm2_enc(sm2para_u)!=1)
				goto err;
			break;
		}//SM2 Enc
		case SM2_ENC_TEST:{
			SM2_Para *sm2para_u = (SM2_Para *)arg;
			if(sm2_enc_test(sm2para_u)!=1)
				goto err;
			break;
		}//SM2 Enc Test

		/**********************************for hmac ***************************************/

		case HMAC_INIT:{
			GM_HMAC *gm_hmac_u = (GM_HMAC *)arg;
			if(hmac_init(gm_hmac_u) != 1)
				goto err;
			break;
		}

		case HMAC_UPDATE:{
			GM_HMAC *gm_hmac_u = (GM_HMAC *)arg;
			if(hmac_update(gm_hmac_u) != 1)
				goto err;
			break;
		}

		case HMAC_FINAL:{
			GM_HMAC *gm_hmac_u = (GM_HMAC *)arg;
			if(hmac_final(gm_hmac_u) != 1)
				goto err;
			break;
		}

		/**********************************for sm4 ***************************************/
		case SM4_OP:{
			SM4_Para *sm4para_u = (SM4_Para *)arg;
			if(sm4_op(sm4para_u)!=1)
				goto err;
			break;
		}
		case SM4_OP_PAD:{
			SM4_Para *sm4para_u = (SM4_Para *)arg;
			if(sm4_op_pad(sm4para_u)!=1)
				goto err;
			break;
		}
		default:
			goto err;	
		
	}
	return 0;
err:
	return -ENOTTY;
}

int __init init_template(void){
	int rc;
	init_template_dev();
	rc = misc_register(template_dev.cdev);
	init_random();
	CEllipticCurveInitParam();
	//IntegrityVerifed = verify_mod_sign();
	IntegrityVerifed = 1;
	printk(KERN_DEBUG "dirver %s loaded\n", module_name);
	return 0;
}

void __exit cleanup_template(void)
{
	misc_deregister(template_dev.cdev);
	printk(KERN_DEBUG "dirver %s unloaded\n",module_name );
}
module_init(init_template);
module_exit(cleanup_template);
