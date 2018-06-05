#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define INITIAL_STRONG_VALUE 0

///////////////////////////////////////////////////////////////////////////////////////////////
template<typename T>
class sp {
public:
    inline sp() : m_ptr(0) {
     printf("sp 默认构造函数!\n"); }

    sp(T* other);
    sp(const sp<T>& other);

    ~sp();

    // Assignment

    sp& operator = (T* other);
    sp& operator = (const sp<T>& other);


    //! Special optimization for use by ProcessState (and nobody else).
    void force_set(T* other);

    // Reset

    void clear();

    // Accessors

    inline  T&      operator* () const  { return *m_ptr; }
    inline  T*      operator-> () const { return m_ptr;  }
    inline  T*      get() const         { return m_ptr; }


private:    
    void set_pointer(T* ptr);
    T* m_ptr;
};


// ---------------------------------------------------------------------------
// No user serviceable parts below here.

template<typename T>
sp<T>::sp(T* other)
        : m_ptr(other) {
printf("sp 拷贝RefBase 指针!\n");
    if (other)
        other->incStrong(this);
}

template<typename T>
sp<T>::sp(const sp<T>& other)
        : m_ptr(other.m_ptr) {
printf("sp 拷贝 sp 对象!\n");
    if (m_ptr)
        m_ptr->incStrong(this);
}

template<typename T>
sp<T>::~sp() {
printf("sp ~sp()!\n");
    if (m_ptr)
        m_ptr->decStrong(this);
}

template<typename T>
sp<T>& sp<T>::operator =(const sp<T>& other) {
printf("sp 重载sp 对象\n");
    T* otherPtr(other.m_ptr);
    if (otherPtr)
        otherPtr->incStrong(this);
    if (m_ptr)
        m_ptr->decStrong(this);
    m_ptr = otherPtr;
    return *this;
}

template<typename T>
sp<T>& sp<T>::operator =(T* other) {
printf("sp 重载RefBase 指针!\n");
    if (other)
        other->incStrong(this);
    if (m_ptr)
        m_ptr->decStrong(this);
    m_ptr = other;
    return *this;
}

template<typename T>
void sp<T>::force_set(T* other) {
printf("sp force_set!\n");
    other->forceIncStrong(this);
    m_ptr = other;
}

template<typename T>
void sp<T>::clear() {
printf("sp clear!\n");
    if (m_ptr) {
        m_ptr->decStrong(this);
        m_ptr = 0;
    }
}

template<typename T>
void sp<T>::set_pointer(T* ptr) {
printf("sp set_pointer!\n");
    m_ptr = ptr;
}

//////////////////////////////////////////////////////////////////////////////////////////
class RefBase
{
public:
            void            incStrong(const void* id) const;
            void            decStrong(const void* id) const;
    
            void            forceIncStrong(const void* id) const;

            //! DEBUGGING ONLY: Get current strong ref count.
            int         getStrongCount() const;

    class weakref_type
    {
    public:
        RefBase*            refBase() const;
        
        void                incWeak(const void* id);
        void                decWeak(const void* id);
        
        // acquires a strong reference if there is already one.
        bool                attemptIncStrong(const void* id);
        
        // acquires a weak reference if there is already one.
        // This is not always safe. see ProcessState.cpp and BpBinder.cpp
        // for proper use.
        bool                attemptIncWeak(const void* id);

        //! DEBUGGING ONLY: Get current weak ref count.
        int             getWeakCount() const;

        //! DEBUGGING ONLY: Print references held on object.
        void                printRefs() const;

        //! DEBUGGING ONLY: Enable tracking for this object.
        // enable -- enable/disable tracking
        // retain -- when tracking is enable, if true, then we save a stack trace
        //           for each reference and dereference; when retain == false, we
        //           match up references and dereferences and keep only the 
        //           outstanding ones.
        
        void                trackMe(bool enable, bool retain);
    };
    
            weakref_type*   createWeak(const void* id) const;
            
            weakref_type*   getWeakRefs() const;

            //! DEBUGGING ONLY: Print references held on object.
    inline  void            printRefs() const { getWeakRefs()->printRefs(); }

            //! DEBUGGING ONLY: Enable tracking of object.
    inline  void            trackMe(bool enable, bool retain)
    { 
        getWeakRefs()->trackMe(enable, retain); 
    }

    typedef RefBase basetype;

protected:
                            RefBase();
    virtual                 ~RefBase();
    
    //! Flags for extendObjectLifetime()
    enum {
        OBJECT_LIFETIME_STRONG  = 0x0000,
        OBJECT_LIFETIME_WEAK    = 0x0001,
        OBJECT_LIFETIME_MASK    = 0x0001
    };
    
            void            extendObjectLifetime(int mode);
            
    //! Flags for onIncStrongAttempted()
    enum {
        FIRST_INC_STRONG = 0x0001
    };
    
    virtual void            onFirstRef();
    virtual void            onLastStrongRef(const void* id);
    virtual bool            onIncStrongAttempted(int flags, const void* id);
    virtual void            onLastWeakRef(const void* id);

private:
    friend class weakref_type;
    class weakref_impl;
    
                            RefBase(const RefBase& o);
            RefBase&        operator=(const RefBase& o);

private:
    friend class ReferenceMover;

    //static void renameRefs(size_t n, const ReferenceRenamer& renamer);

    static void renameRefId(weakref_type* ref,
            const void* old_id, const void* new_id);

    static void renameRefId(RefBase* ref,
            const void* old_id, const void* new_id);

        weakref_impl* const mRefs;
};



///////////////////////////////////////////////////////////////////////////////////////
class RefBase::weakref_impl : public RefBase::weakref_type
{
public:
    volatile int    mStrong;
    volatile int    mWeak;
    RefBase* const      mBase;
    volatile int    mFlags;

#if !DEBUG_REFS

    weakref_impl(RefBase* base)
        : mStrong(INITIAL_STRONG_VALUE)
        , mWeak(0)
        , mBase(base)
        , mFlags(0)
    {
    }

    void addStrongRef(const void* id) { }
    void removeStrongRef(const void* id) { }
    void renameStrongRefId(const void* old_id, const void* new_id) { }
    void addWeakRef(const void* id) { }
    void removeWeakRef(const void* id) { }
    void renameWeakRefId(const void* old_id, const void* new_id) { }
    void printRefs() const { }
    void trackMe(bool, bool) { }

#else

    weakref_impl(RefBase* base)
        : mStrong(INITIAL_STRONG_VALUE)
        , mWeak(0)
        , mBase(base)
        , mFlags(0)
        , mStrongRefs(NULL)
        , mWeakRefs(NULL)
        , mTrackEnabled(!!DEBUG_REFS_ENABLED_BY_DEFAULT)
        , mRetain(false)
    {
    }
    
    ~weakref_impl()
    {
        bool dumpStack = false;
        if (!mRetain && mStrongRefs != NULL) {
            dumpStack = true;
            ALOGE("Strong references remain:");
            ref_entry* refs = mStrongRefs;
            while (refs) {
                char inc = refs->ref >= 0 ? '+' : '-';
                ALOGD("\t%c ID %p (ref %d):", inc, refs->id, refs->ref);
#if DEBUG_REFS_CALLSTACK_ENABLED
                refs->stack.log(LOG_TAG);
#endif
                refs = refs->next;
            }
        }

        if (!mRetain && mWeakRefs != NULL) {
            dumpStack = true;
            ALOGE("Weak references remain!");
            ref_entry* refs = mWeakRefs;
            while (refs) {
                char inc = refs->ref >= 0 ? '+' : '-';
                ALOGD("\t%c ID %p (ref %d):", inc, refs->id, refs->ref);
#if DEBUG_REFS_CALLSTACK_ENABLED
                refs->stack.log(LOG_TAG);
#endif
                refs = refs->next;
            }
        }
        if (dumpStack) {
            ALOGE("above errors at:");
            CallStack stack(LOG_TAG);
        }
    }

    void addStrongRef(const void* id) {
        //ALOGD_IF(mTrackEnabled,
        //        "addStrongRef: RefBase=%p, id=%p", mBase, id);
        addRef(&mStrongRefs, id, mStrong);
    }

    void removeStrongRef(const void* id) {
        //ALOGD_IF(mTrackEnabled,
        //        "removeStrongRef: RefBase=%p, id=%p", mBase, id);
        if (!mRetain) {
            removeRef(&mStrongRefs, id);
        } else {
            addRef(&mStrongRefs, id, -mStrong);
        }
    }

    void renameStrongRefId(const void* old_id, const void* new_id) {
        //ALOGD_IF(mTrackEnabled,
        //        "renameStrongRefId: RefBase=%p, oid=%p, nid=%p",
        //        mBase, old_id, new_id);
        renameRefsId(mStrongRefs, old_id, new_id);
    }

    void addWeakRef(const void* id) {
        addRef(&mWeakRefs, id, mWeak);
    }

    void removeWeakRef(const void* id) {
        if (!mRetain) {
            removeRef(&mWeakRefs, id);
        } else {
            addRef(&mWeakRefs, id, -mWeak);
        }
    }

    void renameWeakRefId(const void* old_id, const void* new_id) {
        renameRefsId(mWeakRefs, old_id, new_id);
    }

    void trackMe(bool track, bool retain)
    { 
        mTrackEnabled = track;
        mRetain = retain;
    }

    void printRefs() const
    {
        String8 text;

        {
            Mutex::Autolock _l(mMutex);
            char buf[128];
            sprintf(buf, "Strong references on RefBase %p (weakref_type %p):\n", mBase, this);
            text.append(buf);
            printRefsLocked(&text, mStrongRefs);
            sprintf(buf, "Weak references on RefBase %p (weakref_type %p):\n", mBase, this);
            text.append(buf);
            printRefsLocked(&text, mWeakRefs);
        }

        {
            char name[100];
            snprintf(name, 100, DEBUG_REFS_CALLSTACK_PATH "/%p.stack", this);
            int rc = open(name, O_RDWR | O_CREAT | O_APPEND, 644);
            if (rc >= 0) {
                write(rc, text.string(), text.length());
                close(rc);
                ALOGD("STACK TRACE for %p saved in %s", this, name);
            }
            else ALOGE("FAILED TO PRINT STACK TRACE for %p in %s: %s", this,
                      name, strerror(errno));
        }
    }

private:
    struct ref_entry
    {
        ref_entry* next;
        const void* id;
#if DEBUG_REFS_CALLSTACK_ENABLED
        CallStack stack;
#endif
        int ref;
    };

    void addRef(ref_entry** refs, const void* id, int3 mRef)
    {
        if (mTrackEnabled) {
            AutoMutex _l(mMutex);

            ref_entry* ref = new ref_entry;
            // Reference count at the time of the snapshot, but before the
            // update.  Positive value means we increment, negative--we
            // decrement the reference count.
            ref->ref = mRef;
            ref->id = id;
#if DEBUG_REFS_CALLSTACK_ENABLED
            ref->stack.update(2);
#endif
            ref->next = *refs;
            *refs = ref;
        }
    }

    void removeRef(ref_entry** refs, const void* id)
    {
        if (mTrackEnabled) {
            AutoMutex _l(mMutex);
            
            ref_entry* const head = *refs;
            ref_entry* ref = head;
            while (ref != NULL) {
                if (ref->id == id) {
                    *refs = ref->next;
                    delete ref;
                    return;
                }
                refs = &ref->next;
                ref = *refs;
            }

            ALOGE("RefBase: removing id %p on RefBase %p"
                    "(weakref_type %p) that doesn't exist!",
                    id, mBase, this);

            ref = head;
            while (ref) {
                char inc = ref->ref >= 0 ? '+' : '-';
                ALOGD("\t%c ID %p (ref %d):", inc, ref->id, ref->ref);
                ref = ref->next;
            }

            CallStack stack(LOG_TAG);
        }
    }

    void renameRefsId(ref_entry* r, const void* old_id, const void* new_id)
    {
        if (mTrackEnabled) {
            AutoMutex _l(mMutex);
            ref_entry* ref = r;
            while (ref != NULL) {
                if (ref->id == old_id) {
                    ref->id = new_id;
                }
                ref = ref->next;
            }
        }
    }

    void printRefsLocked(String8* out, const ref_entry* refs) const
    {
        char buf[128];
        while (refs) {
            char inc = refs->ref >= 0 ? '+' : '-';
            sprintf(buf, "\t%c ID %p (ref %d):\n", 
                    inc, refs->id, refs->ref);
            out->append(buf);
#if DEBUG_REFS_CALLSTACK_ENABLED
            out->append(refs->stack.toString("\t\t"));
#else
            out->append("\t\t(call stacks disabled)");
#endif
            refs = refs->next;
        }
    }

    mutable Mutex mMutex;
    ref_entry* mStrongRefs;
    ref_entry* mWeakRefs;

    bool mTrackEnabled;
    // Collect stack traces on addref and removeref, instead of deleting the stack references
    // on removeref that match the address ones.
    bool mRetain;

#endif
};




////////////////////////////////////////////////////////////////////////////////////////////////////
void RefBase::incStrong(const void* id) const
{
    weakref_impl* const refs = mRefs;
    refs->incWeak(id);
    
    refs->addStrongRef(id);
    const int c = (refs->mStrong)++;
    //ALOG_ASSERT(c > 0, "incStrong() called on %p after last strong ref", refs);
//#if PRINT_REFS
    printf("incStrong of %p from %p: cnt=%d\n", this, id, c);
//#endif
    if (c != INITIAL_STRONG_VALUE)  {
        return;
    }

    //++(-INITIAL_STRONG_VALUE, &refs->mStrong);
    refs->mBase->onFirstRef();
}

void RefBase::decStrong(const void* id) const
{
    weakref_impl* const refs = mRefs;
    refs->removeStrongRef(id);
    const int c = (refs->mStrong)--;
//#if PRINT_REFS
    printf("decStrong of %p from %p: cnt=%d\n", this, id, c);
//#endif
    //ALOG_ASSERT(c >= 1, "decStrong() called on %p too many times", refs);
    if (c == 0) {
        refs->mBase->onLastStrongRef(id);
        if ((refs->mFlags&OBJECT_LIFETIME_MASK) == OBJECT_LIFETIME_STRONG) {
            delete this;
        }
    }
    refs->decWeak(id);
}

void RefBase::forceIncStrong(const void* id) const
{
    weakref_impl* const refs = mRefs;
    refs->incWeak(id);
    
    refs->addStrongRef(id);
    const int c = (refs->mStrong)++;
    //ALOG_ASSERT(c >= 0, "forceIncStrong called on %p after ref count underflow",
               //refs);
//#if PRINT_REFS
    printf("forceIncStrong of %p from %p: cnt=%d\n", this, id, c);
//#endif

    //switch (c) {
    // INITIAL_STRONG_VALUE:
        //android_atomic_add(-INITIAL_STRONG_VALUE, &refs->mStrong);
        // fall through...
    //case 0:
        //refs->mBase->onFirstRef();
    //}
}

int RefBase::getStrongCount() const
{
    return mRefs->mStrong;
}

RefBase* RefBase::weakref_type::refBase() const
{
    return static_cast<const weakref_impl*>(this)->mBase;
}

void RefBase::weakref_type::incWeak(const void* id)
{
    weakref_impl* const impl = static_cast<weakref_impl*>(this);
    impl->addWeakRef(id);
    const int c  = (impl->mWeak)++;
    //ALOG_ASSERT(c >= 0, "incWeak called on %p after last weak ref", this);
}


void RefBase::weakref_type::decWeak(const void* id)
{
    weakref_impl* const impl = static_cast<weakref_impl*>(this);
    impl->removeWeakRef(id);
    const int c = (impl->mWeak)--;
    //ALOG_ASSERT(c >= 1, "decWeak called on %p too many times", this);
    if (c != 1) return;

    if ((impl->mFlags&OBJECT_LIFETIME_WEAK) == OBJECT_LIFETIME_STRONG) {
        // This is the regular lifetime case. The object is destroyed
        // when the last strong reference goes away. Since weakref_impl
        // outlive the object, it is not destroyed in the dtor, and
        // we'll have to do it here.
        if (impl->mStrong == INITIAL_STRONG_VALUE) {
            // Special case: we never had a strong reference, so we need to
            // destroy the object now.
            delete impl->mBase;
        } else {
            // ALOGV("Freeing refs %p of old RefBase %p\n", this, impl->mBase);
            delete impl;
        }
    } else {
        // less common case: lifetime is OBJECT_LIFETIME_{WEAK|FOREVER}
        impl->mBase->onLastWeakRef(id);
        if ((impl->mFlags&OBJECT_LIFETIME_MASK) == OBJECT_LIFETIME_WEAK) {
            // this is the OBJECT_LIFETIME_WEAK case. The last weak-reference
            // is gone, we can destroy the object.
            delete impl->mBase;
        }
    }
}

bool RefBase::weakref_type::attemptIncStrong(const void* id)
{
    incWeak(id);
    
    weakref_impl* const impl = static_cast<weakref_impl*>(this);
    int curCount = impl->mStrong;

    //ALOG_ASSERT(curCount >= 0,
            //"attemptIncStrong called on %p after underflow", this);

    while (curCount > 0 && curCount != INITIAL_STRONG_VALUE) {
        // we're in the easy/common case of promoting a weak-reference
        // from an existing strong reference.
        //if (android_atomic_cmpxchg(curCount, curCount+1, &impl->mStrong) == 0) {
            //break;
        //}
        // the strong count has changed on us, we need to re-assert our
        // situation.
        curCount = impl->mStrong;
    }
    
    if (curCount <= 0 || curCount == INITIAL_STRONG_VALUE) {
        // we're now in the harder case of either:
        // - there never was a strong reference on us
        // - or, all strong references have been released
        if ((impl->mFlags&OBJECT_LIFETIME_WEAK) == OBJECT_LIFETIME_STRONG) {
            // this object has a "normal" life-time, i.e.: it gets destroyed
            // when the last strong reference goes away
            if (curCount <= 0) {
                // the last strong-reference got released, the object cannot
                // be revived.
                decWeak(id);
                return false;
            }

            // here, curCount == INITIAL_STRONG_VALUE, which means
            // there never was a strong-reference, so we can try to
            // promote this object; we need to do that atomically.
           // while (curCount > 0) {
                //if (android_atomic_cmpxchg(curCount, curCount + 1,
                        //&impl->mStrong) == 0) {
                    //break;
                //}
                // the strong count has changed on us, we need to re-assert our
                // situation (e.g.: another thread has inc/decStrong'ed us)
                //curCount = impl->mStrong;
            //}

            if (curCount <= 0) {
                // promote() failed, some other thread destroyed us in the
                // meantime (i.e.: strong count reached zero).
                decWeak(id);
                return false;
            }
        } else {
            // this object has an "extended" life-time, i.e.: it can be
            // revived from a weak-reference only.
            // Ask the object's implementation if it agrees to be revived
            if (!impl->mBase->onIncStrongAttempted(FIRST_INC_STRONG, id)) {
                // it didn't so give-up.
                decWeak(id);
                return false;
            }
            // grab a strong-reference, which is always safe due to the
            // extended life-time.
            curCount = (impl->mStrong)++;
        }

        // If the strong reference count has already been incremented by
        // someone else, the implementor of onIncStrongAttempted() is holding
        // an unneeded reference.  So call onLastStrongRef() here to remove it.
        // (No, this is not pretty.)  Note that we MUST NOT do this if we
        // are in fact acquiring the first reference.
        if (curCount > 0 && curCount < INITIAL_STRONG_VALUE) {
            impl->mBase->onLastStrongRef(id);
        }
    }
    
    impl->addStrongRef(id);

#if PRINT_REFS
    ALOGD("attemptIncStrong of %p from %p: cnt=%d\n", this, id, curCount);
#endif

    // now we need to fix-up the count if it was INITIAL_STRONG_VALUE
    // this must be done safely, i.e.: handle the case where several threads
    // were here in attemptIncStrong().
    curCount = impl->mStrong;
    //while (curCount >= INITIAL_STRONG_VALUE) {
        //ALOG_ASSERT(curCount > INITIAL_STRONG_VALUE,
                //"attemptIncStrong in %p underflowed to INITIAL_STRONG_VALUE",
                //this);
        //if (android_atomic_cmpxchg(curCount, curCount-INITIAL_STRONG_VALUE,
                //&impl->mStrong) == 0) {
            //break;
        //}
        // the strong-count changed on us, we need to re-assert the situation,
        // for e.g.: it's possible the fix-up happened in another thread.
        //curCount = impl->mStrong;
    //}

    return true;
}

bool RefBase::weakref_type::attemptIncWeak(const void* id)
{
    weakref_impl* const impl = static_cast<weakref_impl*>(this);

    int curCount = impl->mWeak;
    //ALOG_ASSERT(curCount >= 0, "attemptIncWeak called on %p after underflow",
               //this);
    //while (curCount > 0) {
        //if (android_atomic_cmpxchg(curCount, curCount+1, &impl->mWeak) == 0) {
            //break;
        //}
        //curCount = impl->mWeak;
    //}

    if (curCount > 0) {
        impl->addWeakRef(id);
    }

    return curCount > 0;
}

int RefBase::weakref_type::getWeakCount() const
{
    return static_cast<const weakref_impl*>(this)->mWeak;
}

void RefBase::weakref_type::printRefs() const
{
    static_cast<const weakref_impl*>(this)->printRefs();
}

void RefBase::weakref_type::trackMe(bool enable, bool retain)
{
    static_cast<weakref_impl*>(this)->trackMe(enable, retain);
}

RefBase::weakref_type* RefBase::createWeak(const void* id) const
{
    mRefs->incWeak(id);
    return mRefs;
}

RefBase::weakref_type* RefBase::getWeakRefs() const
{
    return mRefs;
}

RefBase::RefBase()
    : mRefs(new weakref_impl(this))
{
    printf("RefBase::RefBase 构造函数!\n");
}

RefBase::~RefBase()
{
    printf("RefBase::RefBase 析构函数! mRefs->mStrong = %d\n", mRefs->mStrong);
    if (mRefs->mStrong == INITIAL_STRONG_VALUE) {
        // we never acquired a strong (and/or weak) reference on this object.
        delete mRefs;
    } else {
        // life-time of this object is extended to WEAK or FOREVER, in
        // which case weakref_impl doesn't out-live the object and we
        // can free it now.
        if ((mRefs->mFlags & OBJECT_LIFETIME_MASK) != OBJECT_LIFETIME_STRONG) {
            // It's possible that the weak count is not 0 if the object
            // re-acquired a weak reference in its destructor
            if (mRefs->mWeak == 0) {
                delete mRefs;
            }
        }
    }
    // for debugging purposes, clear this.
    const_cast<weakref_impl*&>(mRefs) = NULL;
}

void RefBase::extendObjectLifetime(int mode)
{
    //android_atomic_or(mode, &mRefs->mFlags);
}

void RefBase::onFirstRef()
{
}

void RefBase::onLastStrongRef(const void* id)
{
}

bool RefBase::onIncStrongAttempted(int flags, const void* id)
{
    return (flags&FIRST_INC_STRONG) ? true : false;
}

void RefBase::onLastWeakRef(const void* id)
{
}

// ---------------------------------------------------------------------------

#if DEBUG_REFS
//void RefBase::renameRefs(size_t n, const ReferenceRenamer& renamer) {
    //for (size_t i=0 ; i<n ; i++) {
        //renamer(i);
    //}
//}
#else
//void RefBase::renameRefs(size_t n, const ReferenceRenamer& renamer) { }
#endif

void RefBase::renameRefId(weakref_type* ref,
        const void* old_id, const void* new_id) {
    weakref_impl* const impl = static_cast<weakref_impl*>(ref);
    impl->renameStrongRefId(old_id, new_id);
    impl->renameWeakRefId(old_id, new_id);
}

void RefBase::renameRefId(RefBase* ref,
        const void* old_id, const void* new_id) {
    ref->mRefs->renameStrongRefId(old_id, new_id);
    ref->mRefs->renameWeakRefId(old_id, new_id);
}


//*/

class test: public RefBase
{
    public:
        test() 
	{
          //a = "a";
	};
        void function() 
	{
	    printf("test funtion !\n");
	    //printf("test funtion %c!\n", a);
	};
	virtual ~test(){};
    private:
        char* a;

};

void testFunction(const sp<test>& t)
{
        printf("testFunction *******begin!\n");
  	sp<test> te = t;
        printf("testFunction *******end!\n");
}

int **makeArray(int row, int clo)
{
	int **A = (int **)malloc(row * sizeof(int));
        printf("makeArray A = %p\n", A);
        for(int i =0; i < row;i++) 
	{
            A[i] = (int *)malloc(clo *sizeof(int));
            printf("makeArray A[%d] = %p\n", i,&A[i]);
            printf("makeArray A[%d] = %x\n", i,A[i]);
	}
        for(int i =0; i < row;i++) 
	{
            for(int j =0; j < clo;j++) 
	    {
                A[i][j] = 2;
	    }
	}	
	return A;
}
void makepointer()
{
	int i = 260;
        printf("i = %d!\n",i);
        char *Add = (char *)&i;
        *(Add +1) = 10;
        printf("i = %d!\n",i);
	//i = 250;
        char arr[10] = "123456";
	char *p = (char *)malloc(sizeof(char));
        memset(p,1,4);
        printf("p = %s!\n",p);
	memcpy(p,"abcde",5);
        printf("p = %s!\n",p);
}
int main()
{
        //printf("strong pointer test!\n");
	//sp<test> t;
        //testFunction(t);
        //t->function();
        //t = new test();
        //testFunction(t);
        //t->function();
        //sp<test> t2;
	//t2 = t;


        printf("int = %d!\n",sizeof(int));
        printf("char = %d!\n",sizeof(char));
        printf("int* = %d!\n",sizeof(int*));
        printf("void* = %d!\n",sizeof(void*));
	makeArray(3, 2);
        //makepointer();
	return 0;
}




