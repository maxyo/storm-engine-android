package org.stormengine.port;

import android.os.Bundle;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.Gravity;
import android.widget.Button;
import android.widget.GridLayout;
import android.widget.RelativeLayout;
import java.util.HashSet;
import org.libsdl.app.SDLActivity;

/** Real engine activity. Button labels describe key bindings, not game actions. */
public final class GameActivity extends SDLActivity {
    private final HashSet<Integer> heldKeys=new HashSet<>();
    private final HashSet<Integer> heldMouse=new HashSet<>();
    @Override protected String[] getLibraries() {
        return new String[]{"c++_shared","SDL2","fmt","mimalloc","dxvk_d3d9","engine"};
    }
    @Override protected void onCreate(Bundle state) {
        super.onCreate(state);
        if(mLayout==null)return;
        GridLayout movement=new GridLayout(this);movement.setColumnCount(3);
        key(movement,"Q",KeyEvent.KEYCODE_Q);key(movement,"W",KeyEvent.KEYCODE_W);key(movement,"E",KeyEvent.KEYCODE_E);
        key(movement,"A",KeyEvent.KEYCODE_A);key(movement,"S",KeyEvent.KEYCODE_S);key(movement,"D",KeyEvent.KEYCODE_D);
        key(movement,"Shift",KeyEvent.KEYCODE_SHIFT_LEFT);key(movement,"Space",KeyEvent.KEYCODE_SPACE);key(movement,"Esc",KeyEvent.KEYCODE_ESCAPE);
        RelativeLayout.LayoutParams left=new RelativeLayout.LayoutParams(-2,-2);left.addRule(RelativeLayout.ALIGN_PARENT_LEFT);left.addRule(RelativeLayout.ALIGN_PARENT_BOTTOM);mLayout.addView(movement,left);
        GridLayout actions=new GridLayout(this);actions.setColumnCount(3);
        key(actions,"F1",KeyEvent.KEYCODE_F1);key(actions,"F2",KeyEvent.KEYCODE_F2);key(actions,"F3",KeyEvent.KEYCODE_F3);
        key(actions,"Enter",KeyEvent.KEYCODE_ENTER);key(actions,"Tab",KeyEvent.KEYCODE_TAB);key(actions,"R",KeyEvent.KEYCODE_R);
        mouse(actions,"LMB",MotionEvent.BUTTON_PRIMARY);mouse(actions,"RMB",MotionEvent.BUTTON_SECONDARY);
        Button look=new Button(this);look.setText("Look");look.setContentDescription("Drag for relative mouse look");look.setAlpha(.6f);
        final float[] last=new float[2];
        look.setOnTouchListener((v,e)->{if(e.getActionMasked()==MotionEvent.ACTION_DOWN){last[0]=e.getX();last[1]=e.getY();}else if(e.getActionMasked()==MotionEvent.ACTION_MOVE){onNativeMouse(0,MotionEvent.ACTION_MOVE,e.getX()-last[0],e.getY()-last[1],true);last[0]=e.getX();last[1]=e.getY();}return true;});actions.addView(look);
        RelativeLayout.LayoutParams right=new RelativeLayout.LayoutParams(-2,-2);right.addRule(RelativeLayout.ALIGN_PARENT_RIGHT);right.addRule(RelativeLayout.ALIGN_PARENT_BOTTOM);mLayout.addView(actions,right);
    }
    private void key(GridLayout layout,String label,int code) {
        Button b=new Button(this);b.setText(label);b.setContentDescription("Hold "+label+" key");b.setAlpha(.6f);
        b.setOnTouchListener((v,e)->{int action=e.getActionMasked();if(action==MotionEvent.ACTION_DOWN){if(heldKeys.add(code))onNativeKeyDown(code);}else if(action==MotionEvent.ACTION_UP||action==MotionEvent.ACTION_CANCEL){if(heldKeys.remove(code))onNativeKeyUp(code);}return true;});layout.addView(b);
    }
    private void mouse(GridLayout layout,String label,int code) {
        Button b=new Button(this);b.setText(label);b.setAlpha(.6f);b.setContentDescription(label+" mouse button");
        b.setOnTouchListener((v,e)->{int action=e.getActionMasked();if(action==MotionEvent.ACTION_DOWN){heldMouse.add(code);onNativeMouse(code,MotionEvent.ACTION_DOWN,0,0,true);}else if(action==MotionEvent.ACTION_UP||action==MotionEvent.ACTION_CANCEL){heldMouse.remove(code);onNativeMouse(code,MotionEvent.ACTION_UP,0,0,true);}return true;});layout.addView(b);
    }
    @Override protected void onPause() {
        for(int code:heldKeys)onNativeKeyUp(code);heldKeys.clear();
        for(int code:heldMouse)onNativeMouse(code,MotionEvent.ACTION_UP,0,0,true);heldMouse.clear();
        super.onPause();
    }
}
