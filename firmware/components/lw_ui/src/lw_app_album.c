/* Photo wall, bounded full-screen viewer and explicit examples. SPDX-License-Identifier: MIT */
#include "lw_shell.h"
#include "lw_theme.h"
#include "lw_widgets.h"
#include "lw_os_design.h"
#include "lw_ui_content.h"
#include "src/misc/cache/instance/lv_image_cache.h"
#include <stdint.h>
#include <string.h>
static const char *const sample_titles[]={"海边","生日","散步","旅行","厨房","夜晚"};
static const uint32_t sample_colors[]={0xf2d9d9,0xe9d9bc,0xd5e2d7,0xdad4e8,0xf0e4c9,0xd7deeb};
static lw_album_source_t source; static bool configured,demo;
static lv_obj_t *root,*overlay,*stage,*caption;
static lw_album_picture_t picture; static unsigned current;
static unsigned count(void) { return demo ? 6 : source.count; }
unsigned lw_album_live_pictures(void) { return picture.image ? 1 : 0; }
bool lw_album_set_source(const lw_album_source_t *value)
{
    if (root || (value && (value->count>64 || (value->count && (!value->title || !value->load || !value->release))))) return false;
    memset(&source,0,sizeof(source)); if (value) source=*value; configured=true; return true;
}
static void release_picture(void)
{ if (picture.image) lv_image_cache_drop(picture.image); if ((picture.image || picture.token) && source.release) source.release(source.context,&picture); memset(&picture,0,sizeof(picture)); }
static void close_view(lv_event_t *e)
{ (void)e; if (overlay) lv_obj_delete(overlay); overlay=stage=caption=NULL; release_picture(); }
static void sample(lv_obj_t *parent,unsigned index,int w,int h)
{
    lv_obj_t *scene=lw_os_box(parent,0,0,w,h,sample_colors[index%6],12,255);
    lw_os_box(scene,w*3/4,h/7,h/6,h/6,0xffefcb,LV_RADIUS_CIRCLE,255);
    lw_os_box(scene,0,h*2/3,w,h/3,index==5 ? 0x8999b6 : 0x9db9b4,0,255);
    lw_os_box(scene,w/8,h/2,w/3,h/4,index==5 ? 0x75849f : 0xb0c7b1,32,255);
    lw_os_icon(scene,OS_I_HEART,w/2-18,h/2-18,36,0xffffff);
}
static void update_view(void)
{
    lv_obj_clean(stage); release_picture(); const lw_palette_t *c=lw_palette();
    const char *title=demo ? sample_titles[current] : source.title(source.context,current);
    lv_label_set_text_fmt(caption,"%s   %u / %u%s",title ? title : "未命名照片",current+1,count(),demo ? " · 示例" : "");
    if (demo) { sample(stage,current,944,400); lw_os_label(stage,"代码绘制示例 · 没有私人照片",&lw_font_18,24,26,880,c->text); }
    else {
        bool loaded=source.load(source.context,current,&picture); const lv_image_dsc_t *image=picture.image;
        if (loaded && image && image->header.magic==LV_IMAGE_HEADER_MAGIC && image->data && image->data_size && image->data_size<=2u*1024u*1024u && image->header.w && image->header.h && image->header.w<=4096 && image->header.h<=4096) {
            lv_obj_t *photo=lv_image_create(stage); lv_image_set_src(photo,image);
            unsigned sx=944u*256u/image->header.w,sy=400u*256u/image->header.h,scale=sx<sy ? sx : sy;
            if (scale>512) scale=512;
            lv_image_set_scale(photo,scale); lv_obj_center(photo);
        } else {
            release_picture(); lw_os_icon(stage,OS_I_ALBUM,432,70,80,c->dim);
            lv_obj_t *label=lw_os_label(stage,"照片读取失败",&lw_font_28,180,172,584,c->text); lv_obj_set_style_text_align(label,LV_TEXT_ALIGN_CENTER,0);
            lw_os_label(stage,"文件可能缺失或格式不支持。可重试，也可以继续翻页。",&lw_font_18,200,232,560,c->dim);
        }
    }
    lw_os_motion(stage,LW_MOVE_PHOTO);
}
static void next(lv_event_t *e) { (void)e; if (count()) { current=(current+1)%count(); update_view(); } }
static void previous(lv_event_t *e) { (void)e; if (count()) { current=(current+count()-1)%count(); update_view(); } }
static void retry(lv_event_t *e) { (void)e; update_view(); }
static void swipe(lv_event_t *e)
{ (void)e; lv_indev_t *input=lv_indev_active(); if (!input) return; lv_dir_t dir=lv_indev_get_gesture_dir(input); if (dir==LV_DIR_LEFT) next(NULL); else if (dir==LV_DIR_RIGHT) previous(NULL); }
static void view_button(const char *text,int x,int width,lv_event_cb_t cb)
{ lv_obj_t *b=lw_button(overlay,text,width,52,lw_palette()->card,lw_palette()->text,&lw_font_18); lv_obj_set_pos(b,x,528); lv_obj_add_event_cb(b,cb,LV_EVENT_CLICKED,NULL); }
static void open_view(lv_event_t *e)
{
    current=(unsigned)(uintptr_t)lv_event_get_user_data(e); if (current>=count()) return;
    close_view(NULL); overlay=lw_os_box(lv_screen_active(),0,0,1024,600,0x222533,0,255); lv_obj_add_flag(overlay,LV_OBJ_FLAG_CLICKABLE); lv_obj_remove_flag(overlay,LV_OBJ_FLAG_GESTURE_BUBBLE);
    caption=lw_os_label(overlay,"",&lw_font_22,40,28,944,0xf7efe6);
    lv_label_set_long_mode(caption,LV_LABEL_LONG_MODE_DOTS); lv_obj_set_height(caption,40);
    stage=lw_os_box(overlay,40,96,944,400,lw_palette()->card_soft,16,255); lv_obj_add_flag(stage,LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(overlay,swipe,LV_EVENT_GESTURE,NULL);
    view_button("上一张",40,160,previous); view_button("下一张",216,160,next); view_button("重试",664,144,retry); view_button("关闭",824,160,close_view); update_view();
}
static void build(void);
static void show_samples(lv_event_t *e) { (void)e; demo=true; build(); }
static void show_real(lv_event_t *e) { (void)e; demo=false; build(); }
static void build(void)
{
    close_view(NULL); lv_obj_clean(root); const lw_palette_t *c=lw_palette();
    lw_os_label(root,"把喜欢的瞬间，慢慢收好。",&lw_font_28,40,14,760,c->text);
    lw_os_label(root,demo ? "示例照片墙 · 不是真实私人照片" : "本地相册 · 只读取明确提供的照片",&lw_font_18,40,58,800,c->dim);
    if (!count()) {
        lw_os_icon(root,OS_I_ALBUM,460,114,92,c->accent);
        lw_os_label(root,"这里，等你的第一张照片",&lw_font_28,304,224,680,c->text);
        lw_os_label(root,"照片还没导入也没关系，先把位置留好。",&lw_font_18,330,272,640,c->dim);
        lv_obj_t *b=lw_button(root,"查看示例",200,52,c->accent,0xffffff,&lw_font_18); lv_obj_set_pos(b,412,328); lv_obj_add_event_cb(b,show_samples,LV_EVENT_CLICKED,NULL); return;
    }
    unsigned shown=count()<6 ? count() : 6;
    for (unsigned i=0;i<shown;i++) {
        int x=40+(int)(i%3)*324,y=96+(int)(i/3)*154;
        lv_obj_t *card=lw_card(root,x,y,296,142); lv_obj_set_style_radius(card,12,0); lv_obj_add_flag(card,LV_OBJ_FLAG_CLICKABLE);
        lw_os_motion_touch(card); lv_obj_add_event_cb(card,open_view,LV_EVENT_CLICKED,(void *)(uintptr_t)i);
        if (demo) { lv_obj_t *thumb=lw_os_box(card,10,10,276,86,0,0,0); sample(thumb,i,276,86); }
        else lw_os_icon(card,OS_I_ALBUM,126,26,44,c->accent);
        const char *title=demo ? sample_titles[i] : source.title(source.context,i); lv_obj_t *title_label=lw_os_label(card,title ? title : "未命名照片",&lw_font_18,14,108,262,c->text);
        lv_label_set_long_mode(title_label,LV_LABEL_LONG_MODE_DOTS); lv_obj_set_height(title_label,26);
    }
    lv_obj_t *b=lw_button(root,demo ? "返回照片列表" : "从第一张全屏浏览",236,44,c->card_soft,c->text,&lw_font_18); lv_obj_set_pos(b,748,400); lv_obj_add_event_cb(b,demo ? show_real : open_view,LV_EVENT_CLICKED,NULL);
    lw_os_label(root,demo ? "示例仅展示布局。真实照片不会自动上传。" : "缩略列表最多显示六张；全屏可以浏览全部照片。",&lw_font_18,40,412,700,c->dim);
}
static void create(lv_obj_t *content,const lw_app_t *self) { (void)self; root=content; demo=!configured && !lw_shell_host_active(); build(); }
static void destroy(void) { close_view(NULL); root=NULL; }
const lw_app_t lw_app_album_def={"album","相册",create,destroy};
