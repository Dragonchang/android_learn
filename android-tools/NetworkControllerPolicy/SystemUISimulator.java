package com.android.systemui.statusbar.policy;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.os.Handler;
import com.android.internal.telephony.IccCardConstants;
import android.telephony.TelephonyManager;
import android.util.Slog;
import com.htc.htcjavaflag.HtcBuildFlag;

class SystemUISimulator {

    private static final String TAG = "SystemUISimulator";
    private final boolean HTC_DEBUG = HtcBuildFlag.Htc_DEBUG_flag;

    private final Handler mHandler = new Handler();
    private BroadcastReceiver mIntentReceiver;
    private Callback mController;

    // simulated items
    private boolean mHasCsService, mHasPsService;
    private int mPhoneType;
    private IccCardConstants.State mSimState;
    private int mNetworkId;
    private String mSectorId;
    private int mNetworkType;
    private int mDataState;
    private int mDataActivity;
    private int mSignalLevel;
    private boolean mRoamingGsm;
    private int mRoamingCdma;
    private int mCallState;
    private String mNetworkOperator, mSimOperator, mNetworkCountry;
    private boolean mAirplaneMode;
    private int mUnderSilentReset;
    private int sku_id;
    private int region;
    private int inetCondition;
    private boolean lteCa;
    private boolean show4gForLte;
    private boolean show3gForEvdo;
    /* CMCC SGLTE start */
    private boolean mHasMdmService, mHasQscService;
    private int mMdmNetworkType, mQscNetworkType;
    private int mQscSignalLevel;
    private int mModemState;
    public boolean hasMdmService() {return mHasMdmService;}
    public boolean hasQscService() {return mHasQscService;}
    public int getMdmNetworkType() {return mMdmNetworkType;}
    public int getQscNetworkType() {return mQscNetworkType;}
    public int getQscSignalLevel() {return mQscSignalLevel;}
    public int getModemState() {return mModemState;}
    /* CMCC SGLTE end */

    public boolean hasCsService() {return mHasCsService;}
    public boolean hasPsService() {return mHasPsService;}
    public int getPhoneType() {return mPhoneType;}
    public IccCardConstants.State getSimState() {return mSimState;}
    public int getNetworkId() {return mNetworkId;}
    public String getSectorId() {return mSectorId;}
    public int getNetworkType() {return mNetworkType;}
    public int getDataState() {return mDataState;}
    public int getDataActivity() {return mDataActivity;}
    public int getSignalLevel() {return mSignalLevel;}
    public boolean isRoamingGsm() {return mRoamingGsm;}
    public int getCdmaRoamingIndicator() {return mRoamingCdma;}
    public int getCallState() {return mCallState;}
    public String getNetworkOperator() {return mNetworkOperator;}
    public String getNetworkCountry() {return mNetworkCountry;}
    public String getSimOperator() {return mSimOperator;}
    public boolean isAirplaneMode() {return mAirplaneMode;}
    public int getUnderSilentReset() {return mUnderSilentReset;}
    public int getSkuId() {return sku_id;}
    public int getRegion() {return region;}
    public int getInetCondition() {return inetCondition;}
    public boolean getLTECA() {return lteCa;}
    public boolean show4gForLte() {return show4gForLte;}
    public boolean show3gForEvdo() {return show3gForEvdo;}

    public interface Callback {
        public void startSimulation();
        public void stopSimulation();
        public void triggerIconUpdate();
    }

    public SystemUISimulator(Context context) {
        // internal use only
        if (!HTC_DEBUG) return;

        IntentFilter filter = new IntentFilter();
        filter.addAction("com.android.systemui.simulator.action_change");

        mIntentReceiver = new BroadcastReceiver() {
            public void onReceive(Context context, Intent intent) {
                mHandler.post(new SimulationTask(intent, mController));
            }
        };

        if (context != null) {
            context.registerReceiver(mIntentReceiver, filter,
                com.android.systemui.Permission.PERMISSION_APP_PLATFORM, mHandler);
        }
    }

    public void register(Callback controller) {
        mController = controller;
    }

    private class SimulationTask implements Runnable {

        private Intent mIntent;
        private Callback mController;

        public SimulationTask(Intent intent, Callback controller) {
            mIntent = intent;
            mController = controller;
        }

        @Override
        public void run() {

            final Intent intent = mIntent;

            if (intent == null)
                return;

            boolean enabled = intent.getBooleanExtra("SIMULATION_ENABLED", false);

            if (enabled) {

                // sku id
                SystemUISimulator.this.sku_id = intent.getIntExtra("SIMULATE_SKU_ID", 0);

                //region
                SystemUISimulator.this.region = intent.getIntExtra("SIMULATE_REGION", 0);

                // has service
                SystemUISimulator.this.mHasCsService = intent.getBooleanExtra("SIMULATE_HAS_CS_SERVICE", true);
                SystemUISimulator.this.mHasPsService = intent.getBooleanExtra("SIMULATE_HAS_PS_SERVICE", true);

                // sim state
                String stateStr = intent.getStringExtra("SIMULATE_SIM_STATE");
                IccCardConstants.State state = IccCardConstants.State.READY;
                try {
                    SystemUISimulator.this.mSimState = (IccCardConstants.State) IccCardConstants.State.class.getField(stateStr).get(state);
                } catch (Exception e) {e.printStackTrace();}

                // Network ID
                SystemUISimulator.this.mNetworkId = intent.getIntExtra("SIMULATE_NETWORK_ID", 0);

                // Sector ID
                SystemUISimulator.this.mSectorId = intent.getStringExtra("SIMULATE_SECTOR_ID");

                // Airplane mode
                SystemUISimulator.this.mAirplaneMode = intent.getBooleanExtra("SIMULATE_AIRPLANE_MODE", false);

                // network type
                SystemUISimulator.this.mNetworkType = intent.getIntExtra("SIMULATE_NETWORK_TYPE", -1);

                // data state
                SystemUISimulator.this.mDataState = intent.getIntExtra("SIMULATE_DATA_STATE", -1);

                // data activity
                SystemUISimulator.this.mDataActivity = intent.getIntExtra("SIMULATE_DATA_ACTIVITY", -1);

                // signal strength
                SystemUISimulator.this.mSignalLevel = intent.getIntExtra("SIMULATE_SIGNAL_LEVEL", 0);

                // phone type
                SystemUISimulator.this.mPhoneType = intent.getIntExtra("SIMULATE_PHONE_TYPE", 0) + 1;

                // network operator
                SystemUISimulator.this.mNetworkOperator = intent.getStringExtra("SIMULATE_NETWORK_OPERATOR");

                // network country
                SystemUISimulator.this.mNetworkCountry = intent.getStringExtra("SIMULATE_NETWORK_COUNTRY");

                // SIM operator
                SystemUISimulator.this.mSimOperator = intent.getStringExtra("SIMULATE_SIM_OPERATOR");

                // roaming gsm
                SystemUISimulator.this.mRoamingGsm = (1 == intent.getIntExtra("SIMULATE_NETWORK_ROAMING", -1));

                // roaming cdma
                SystemUISimulator.this.mRoamingCdma = intent.getIntExtra("SIMULATE_ERI_INDICATOR", 1);

                // call state
                SystemUISimulator.this.mCallState = intent.getIntExtra("SIMULATE_CALL_STATE", 0);

                // under silent reset
                SystemUISimulator.this.mUnderSilentReset = intent.getIntExtra("SIMULATE_UNDER_SILENT_RESET", 0);

                // Data inetcondition
                SystemUISimulator.this.inetCondition = intent.getIntExtra("SIMULATE_INETCONDITION", 0);

                // LTE CA
                SystemUISimulator.this.lteCa = intent.getBooleanExtra("SIMULATE_LTECA", false);

                // show 4g for lte
                SystemUISimulator.this.show4gForLte = intent.getBooleanExtra("SIMULATE_SHOW_4G_FOR_LTE", false);

                // show 3g for evdo
                SystemUISimulator.this.show3gForEvdo = intent.getBooleanExtra("SIMULATE_SHOW_3G_FOR_EVDO", false);

                /* CMCC SGLTE start */
                SystemUISimulator.this.mHasMdmService =
                    intent.getBooleanExtra("SIMULATE_MDM_STATE", true);
                SystemUISimulator.this.mHasQscService =
                    intent.getBooleanExtra("SIMULATE_QSC_STATE", true);
                SystemUISimulator.this.mMdmNetworkType =
                    intent.getIntExtra("SIMULATE_MDM_NETWORK_TYPE", -1);
                SystemUISimulator.this.mQscNetworkType =
                    intent.getIntExtra("SIMULATE_QSC_NETWORK_TYPE", -1);
                SystemUISimulator.this.mQscSignalLevel =
                    intent.getIntExtra("SIMULATE_QSC_SIGNAL_LEVEL", 0);
                SystemUISimulator.this.mModemState =
                    intent.getIntExtra("SIMULATE_MODEM_STATE", 0);
                /* CMCC SGLTE end */

                // dump intent
                SystemUISimulator.this.dump();
            }

            // trigger update
            final Callback cb = mController;
            if (cb != null) {
                if (enabled) {
                    cb.startSimulation();
                } else {
                    cb.stopSimulation();
                }
                cb.triggerIconUpdate();
            }
        }

    }

    public void dump() {
        Slog.d(TAG, "Simulate(sku_id=" + sku_id +
                    " region=" + region +
                    " airplane=" + mAirplaneMode +
                    " cs=" + mHasCsService +
                    " ps=" + mHasPsService +
                    " phoneType=" + mPhoneType +
                    " simState=" + mSimState +
                    " networkId=" + mNetworkId +
                    " sectorId=" + mSectorId +
                    " netType=" + TelephonyManager.getNetworkTypeName(mNetworkType) +
                    " dataState=" + mDataState +
                    " dataActivity=" + mDataActivity +
                    " signalLevel=" + mSignalLevel +
                    " cellOperator=" + mNetworkOperator +
                    " cellCountry=" + mNetworkCountry +
                    " simOperator=" + mSimOperator +
                    " roamingGsm=" + mRoamingGsm +
                    " roamingCdma=" + mRoamingCdma +
                    " callState=" + mCallState +
                    " inetCondition=" + inetCondition +
                    " lteCa=" + lteCa +
                    " show4gForLte=" + show4gForLte +
                    " show3gForEvdo=" + show3gForEvdo +
                    /* CMCC SGLTE start */
                    " modemState=" + mModemState +
                    " mdmService=" + mHasMdmService +
                    " qscService=" + mHasQscService +
                    " mdmNetType=" + TelephonyManager.getNetworkTypeName(mMdmNetworkType) +
                    " qscNetType=" + TelephonyManager.getNetworkTypeName(mQscNetworkType) +
                    " qscSignalLevel=" + mQscSignalLevel +
                    /* CMCC SGLTE end */
                    " silent_reset=" + mUnderSilentReset + ")");
    }

}
