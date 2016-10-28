//
// Created by hdq on 16/10/26.
//

#include <jni.h>
#include <stdio.h>
#include <git2/remote.h>
#import "time.h"
#include "git2.h"
#include "remote.h"
#include "object.h"
#include "refdb.h"
#include "commit.h"

int lookUpReferenceWithName( git_reference **ref,git_repository *repo){

    //获取所有本地分支名
    git_strarray refStrings;
    int gitError = git_reference_list(&refStrings, repo);
    if (gitError < GIT_OK){
        git_strarray_free(&refStrings);
        return 0;
    }

    char * localNamePrefix = "refs/heads/";

    for (int i = 0; i < refStrings.count ; ++i) {
        size_t len_prefix = strlen(localNamePrefix);
        size_t len_ref = strlen(refStrings.strings[i]);
        int isLocal = 1;
        for (int j = 0; j < len_prefix && j < len_ref; ++j) {
            if (refStrings.strings[i][j] != localNamePrefix[j]){
                isLocal = 0;
                break;
            }
        }

        if (!isLocal)
            continue;

        gitError = git_reference_lookup(ref,repo,refStrings.strings[i]);
        if (gitError != GIT_OK){
            continue;
        }
        break;
    }

    git_strarray_free(&refStrings);
    return gitError;
}

void sendError(JNIEnv* env,jobject thiz, char *string){

    jclass clazz = (*env)->GetObjectClass(env,thiz);
    jmethodID memFunc = (*env)->GetMethodID(env, clazz,"updateStatus", "(Ljava/lang/String;)V");
    (*env)->CallVoidMethod(env, thiz, memFunc,(*env)->NewStringUTF(env,string));

}

typedef struct {
    JNIEnv *env;
    jobject  *thiz;
    char g_user[20];
    char g_password[20];
} GTRemoteConnectionInfo;

int GTCredentialAcquireCallback(git_cred **cred, const char *url, const char *username_from_url, unsigned int allowed_types, void *payload) {

    GTRemoteConnectionInfo  *connectionInfo = payload;
    int gitError = git_cred_userpass_plaintext_new(cred, connectionInfo->g_user, connectionInfo->g_password);
    if (gitError < GIT_OK){
        return gitError;
    }
    return GIT_OK;
}

int GTRemoteFetchTransferProgressCallback(const git_transfer_progress *stats, void *payload) {

    GTRemoteConnectionInfo  *connectionInfo = payload;

    float ro = stats->received_objects;
    float to = stats->total_objects;
    float r = ro/to;
    char log[30] = {0};
    sprintf(log,"r:%f",r);
    sendError(connectionInfo->env,connectionInfo->thiz,log);

    return 0;//(stop == YES ? GIT_EUSER : 0);
}

int isAlreadySync(git_object *local , git_object *remote){

    int ret = 0;
    char *SHA1 = malloc(GIT_OID_HEXSZ);
    if (SHA1 == NULL) return ret;
    git_oid_fmt(SHA1, git_object_id(local));

    char *SHA2 = malloc(GIT_OID_HEXSZ);
    if (SHA2 == NULL) {
        free(SHA1);
        return ret;
    }
    git_oid_fmt(SHA2, git_object_id(remote));

    if(!strcmp(SHA1,SHA2)){
        ret = 1;
    }

    free(SHA2);
    free(SHA1);
    return ret;
}

git_commit *targetCommit(git_reference *ref){
    git_object *obj;
    if (git_reference_peel(&obj,ref,GIT_OBJ_ANY) != GIT_OK) {
        return NULL;
    }
    git_object *commit;
    int gitError = git_object_lookup(&commit, obj->repo, git_object_id(obj), GIT_OBJ_COMMIT);
    git_object_free(obj);

    if (gitError < GIT_OK) {
        return NULL;
    }
    return (git_commit *)commit;
}

int analyzeMerge(git_merge_analysis_t *analysis,git_reference *ref){

    git_commit *obj = targetCommit(ref);
    if (!obj) {
        return 0;
    }
    git_annotated_commit *annotatedCommit;

    git_annotated_commit_lookup(&annotatedCommit,obj->object.repo,git_object_id(obj));

    git_merge_preference_t preference = GIT_MERGE_PREFERENCE_NONE;

    // Merge analysis
    int gitError = git_merge_analysis(analysis, &preference, obj->object.repo, (const git_annotated_commit **) &annotatedCommit, 1);
    if (gitError != GIT_OK) {
        return 0;
    }
    // Cleanup
    git_annotated_commit_free(annotatedCommit);
    return 1;
}

git_reference *referenceByUpdatingTarget(git_reference *ref, char *newTarget, char *message){

    git_reference *newRef = NULL;
    int gitError;
    if (git_reference_type(ref) == GIT_REF_OID) {
        git_oid oid;
        if (git_oid_fromstr(&oid, newTarget)) {
            return NULL;
        }
        gitError = git_reference_set_target(&newRef, ref, &oid, message);
    } else {
        gitError = git_reference_symbolic_set_target(&newRef, ref, newTarget, message);
    }

    if (gitError != GIT_OK) {
        return NULL;
    }
    return newRef;
}

int checkoutReference(git_reference *ref,git_checkout_strategy_t t){
    git_object *targetobj;
    if (git_reference_peel(&targetobj,ref,GIT_OBJ_ANY) != GIT_OK) {
        return 0;
    }
    git_object *obj;
    int gitError = git_object_lookup(&obj, targetobj->repo, git_object_id(targetobj), GIT_OBJ_ANY);
    git_object_free(targetobj);

    if (gitError < GIT_OK) {
        return 0;
    }

    git_checkout_options checkoutOptions = GIT_CHECKOUT_OPTIONS_INIT;
    checkoutOptions.checkout_strategy = t;

    gitError = git_checkout_tree(obj->repo, obj, &checkoutOptions);
    if (gitError == GIT_OK){
        gitError = git_repository_set_head_detached(obj->repo, git_object_id(obj));
    }
    git_object_free(obj);

    return gitError == GIT_OK ? 1 : 0;
}

int createCommitWithTree(git_tree *tree, char *message,git_commit *lc,git_commit *rc, char*name){

    git_signature signature;
    int status = git_signature_new(&signature, "QXB_ANDROID", "huang@qxbapp.com", time(NULL), 8);
    if (status != GIT_OK) return 0;
    git_commit *parentCommits[2];
    parentCommits[0] = lc;
    parentCommits[1] = rc;

    git_oid oid;
    int gitError = git_commit_create(&oid, tree->object.repo, name, &signature, &signature, "UTF-8", message, tree, 2, parentCommits);

    free(parentCommits);
    return gitError == GIT_OK ? 1 : 0;
}

int mergeBranchIntoCurrentBranch(JNIEnv* env, jobject thiz, git_reference *ref){

    git_reference *trackingRef = NULL;
    int gitError = git_branch_upstream(&trackingRef,ref);
    if (gitError != GIT_OK){
        char buff[100]={0};
        sprintf(buff,"Tracking branch not found for %s",ref->name);
        sendError(env,thiz,buff);
        return 0;
    }

    git_commit *local_obj = targetCommit(ref);
    if (!local_obj) {
        sendError(env,thiz,"local branh commit id read fail");
        return 0;
    }
    git_commit *remote_obj = targetCommit(trackingRef);
    if (!remote_obj) {
        sendError(env,thiz,"remote branh commit id read fail");
        return 0;
    }

    if (isAlreadySync(local_obj,remote_obj)){
        sendError(env,thiz,"AlreadySync");
        return 1;
    }
    git_merge_analysis_t analysis = GIT_MERGE_ANALYSIS_NONE;

    if(!analyzeMerge(&analysis,trackingRef)){

        sendError(env,thiz,"analyzeMerge fail");
        git_commit_free(local_obj);
        git_commit_free(remote_obj);
        git_reference_free(trackingRef);
        return 0;
    }
    if (analysis & GIT_MERGE_ANALYSIS_UP_TO_DATE) {
        // Nothing to do

    } else if (analysis & GIT_MERGE_ANALYSIS_FASTFORWARD ||
               analysis & GIT_MERGE_ANALYSIS_UNBORN) {
        // Fast-forward branch

        char buff[100]={0};
        sprintf(buff,"merge %s: Fast-forward",trackingRef->name);
        char *SHA1 = malloc(GIT_OID_HEXSZ);
        if (SHA1 == NULL){
            sendError(env,thiz,"mem alloc fail");
            git_commit_free(local_obj);
            git_commit_free(remote_obj);

            git_reference_free(trackingRef);
            return 0;
        }
        git_oid_fmt(SHA1, git_object_id(remote_obj));
        git_reference *reference = referenceByUpdatingTarget(ref,SHA1,buff);
        int checkoutSuccess = checkoutReference(reference,GIT_CHECKOUT_FORCE);
        return checkoutSuccess;
    } else if (analysis & GIT_MERGE_ANALYSIS_NORMAL) {
        git_tree *localTree = NULL;
        gitError = git_commit_tree(&localTree, local_obj);
        if (gitError < GIT_OK) {
            return 0;
        }
        git_tree *remoteTree = NULL;
        gitError = git_commit_tree(&remoteTree, remote_obj);
        if (gitError < GIT_OK) {
            git_tree__free(localTree);
            git_commit_free(local_obj);
            git_commit_free(remote_obj);

            git_reference_free(trackingRef);
            return 0;
        }
        git_tree *ancestorTree = NULL;
        git_index *index;
        int result = git_merge_trees(&index, ref->db->repo , ancestorTree, localTree,remoteTree, NULL);
        if (result != GIT_OK || index == NULL) {

            sendError(env,thiz,"git_merge_trees fail");
            git_tree__free(localTree);
            git_tree__free(remoteTree);
            git_commit_free(local_obj);
            git_commit_free(remote_obj);
            git_reference_free(trackingRef);
            return 0;
        }

        git_oid oid;
        int status = git_index_write_tree_to(&oid, index, ref->db->repo);
        if (status != GIT_OK) {
            sendError(env,thiz, "Failed to write index to repository");
            git_tree__free(localTree);
            git_tree__free(remoteTree);
            git_commit_free(local_obj);
            git_commit_free(remote_obj);
            git_reference_free(trackingRef);
            return 0;
        }
        git_object *newtree;
        gitError = git_object_lookup(&newtree, ref->db->repo, &oid, GIT_OBJ_TREE);
        if (gitError < GIT_OK) {
            sendError(env,thiz, "Failed to lookup object in repository");
            git_commit_free(local_obj);
            git_commit_free(remote_obj);
            git_reference_free(trackingRef);
            git_tree__free(newtree);
            git_tree__free(localTree);
            git_tree__free(remoteTree);
            return 0;
        }
        int ret = createCommitWithTree((git_tree *)newtree,"Merge branch",localTree,remoteTree,ref->name);

        if (!ret){
            sendError(env,thiz, "Failed to commit in repository");
            git_commit_free(local_obj);
            git_commit_free(remote_obj);
            git_reference_free(trackingRef);
            git_tree__free(newtree);
            git_tree__free(localTree);
            git_tree__free(remoteTree);
            return 0;
        }

        ret = checkoutReference(ref,GIT_CHECKOUT_FORCE);
        if (!ret){
            sendError(env,thiz, "Failed to checkout");
            git_tree__free(newtree);
            git_tree__free(localTree);
            git_tree__free(remoteTree);
            git_commit_free(local_obj);
            git_commit_free(remote_obj);
            git_reference_free(trackingRef);
            return 0;
        }
        git_tree__free(newtree);
        git_tree__free(localTree);
        git_tree__free(remoteTree);
    }

    git_commit_free(local_obj);
    git_commit_free(remote_obj);
    git_reference_free(trackingRef);

    return 1;
}


int fetchRemote(JNIEnv* env, jobject thiz,git_reference *ref,git_remote *remote,const char *user,const char *password){

    GTRemoteConnectionInfo connectionInfo;

    connectionInfo.env = env;
    connectionInfo.thiz = thiz;


    git_remote_callbacks remote_callbacks = {
            .version = GIT_REMOTE_CALLBACKS_VERSION,
            .credentials = NULL,
            .transfer_progress = GTRemoteFetchTransferProgressCallback,
            .payload = &connectionInfo,
    };

    if (user && password){
        memcpy(connectionInfo.g_user,user, strlen(user)+1);
        memcpy(connectionInfo.g_password,password, strlen(password)+1);
        remote_callbacks.credentials = GTCredentialAcquireCallback;
    } else{
        memset(connectionInfo.g_user,0, sizeof(connectionInfo.g_user));
        memset(connectionInfo.g_password,0, sizeof(connectionInfo.g_password));
    }

    git_fetch_options fetchOptions = GIT_FETCH_OPTIONS_INIT;
    fetchOptions.callbacks = remote_callbacks;
    fetchOptions.download_tags = GIT_REMOTE_DOWNLOAD_TAGS_UNSPECIFIED;
    fetchOptions.prune = GIT_FETCH_PRUNE_UNSPECIFIED;

    //获取远端分支内容
    git_strarray refspecs;
    int gitError = git_remote_get_fetch_refspecs(&refspecs, remote);
    if (gitError != GIT_OK) {
        git_strarray_free(&refspecs);
        sendError(env,thiz,"git_remote_get_fetch_refspecs error");
        return 0;
    }

    gitError = git_remote_fetch(remote, &refspecs, &fetchOptions, "fetching remote");

    if (gitError != GIT_OK) {
        char buff[200]={0};
        sprintf(buff,"Failed to fetch from remote , gitError : %d",gitError);
        sendError(env,thiz,buff);
        git_strarray_free(&refspecs);
        return 0;
    }
    git_strarray_free(&refspecs);

    mergeBranchIntoCurrentBranch(env,thiz,ref);

    return 1;
}

int pullBranch(JNIEnv* env, jobject thiz,git_reference *ref,git_remote *remote,const char *user,const char *password){

    int ret;
    ret = fetchRemote(env,thiz,ref,remote,user,password);

    return ret;
}

JNIEXPORT void JNICALL Java_com_nanxin_hdq_libgit2_Libgit2Utils_initLibgit2( JNIEnv* env, jobject thiz) {
    git_libgit2_init();
}
JNIEXPORT void JNICALL Java_com_nanxin_hdq_libgit2_Libgit2Utils_updateRepo( JNIEnv* env, jobject thiz,jstring path,jstring remoteName,jstring user,jstring password){


    const char* pcUtf = (*env)->GetStringUTFChars(env,path, 0);
    if (!pcUtf || strlen(pcUtf) == 0){
        sendError(env,thiz,"error path");
        return;
    }
    git_repository *repository = NULL;

    int gitError = git_repository_open(&repository,pcUtf);
    if (gitError < GIT_OK){
        char buff[200]={0};
        sprintf(buff,"git_repository_open error: %s , gitError : %d",pcUtf,gitError);
        sendError(env,thiz,buff);
        (*env)->ReleaseStringUTFChars(env,path,pcUtf);
        git_repository_free(repository);
        return;
    }
    (*env)->ReleaseStringUTFChars(env,path,pcUtf);

    //遍历分支名取出第一个本地分支
    git_reference *ref;
    gitError = lookUpReferenceWithName(&ref,repository);

    if (!ref){
        char buff[200]={0};
        sprintf(buff,"lookUpReferenceWithName gitError : %d",gitError);
        sendError(env,thiz,buff);
        git_repository_free(repository);
        return;
    }

    //通过传进来的远端仓库名取出远端仓库
    git_remote *remote;

    const char* remotename = (*env)->GetStringUTFChars(env,remoteName, 0);
    gitError = git_remote_lookup(&remote, repository, remotename);
    if (gitError != GIT_OK) {
        sendError(env,thiz,"git_remote_lookup error");
        (*env)->ReleaseStringUTFChars(env,remoteName,remotename);
        git_reference_free(ref);
        git_remote_free(remote);
        git_repository_free(repository);
        return;
    }
    (*env)->ReleaseStringUTFChars(env,remoteName,remotename);
    const char* c_user = NULL;
    const char* c_password = NULL;
    if (user && password){
        c_user = (*env)->GetStringUTFChars(env,user, 0);
        c_password = (*env)->GetStringUTFChars(env,password, 0);
    }

    int ret = pullBranch(env,thiz,ref,remote,c_user,c_password);

    if (user && password){
        (*env)->ReleaseStringUTFChars(env,user,c_user);
        (*env)->ReleaseStringUTFChars(env,password,c_password);
    }

    if (!ret){
        sendError(env,thiz,"pullBranch error");
        git_reference_free(ref);
        git_remote_free(remote);
        git_repository_free(repository);
        return;
    }

    git_reference_free(ref);
    git_remote_free(remote);
    git_repository_free(repository);

    sendError(env,thiz,"pullBranch Done");
}
