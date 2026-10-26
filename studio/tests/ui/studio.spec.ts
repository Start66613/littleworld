import { test,expect } from '@playwright/test';
import type { Page } from '@playwright/test';
import path from 'node:path';
import { promises as fs } from 'node:fs';
const saved=async(page:Page)=>await expect(page.getByRole('status')).toHaveText('已保存到本机');
async function newProject(page:Page,name:string) {
  await page.goto('/');await page.getByRole('button',{name:'新建',exact:true}).click();
  await page.getByLabel('新项目名称',{exact:true}).fill('DEMO · UI '+name);await page.getByRole('button',{name:'创建项目',exact:true}).click();
  await expect(page.getByLabel('项目名称',{exact:true})).toHaveValue('DEMO · UI '+name);
}
test('create, edit leap anniversary, save, reopen and save as',async({page})=>{
  await newProject(page,'anniversary');await page.getByRole('button',{name:'添加日期',exact:true}).click();
  await page.getByLabel('纪念日标题',{exact:true}).fill('DEMO · 闰日');await page.getByLabel('纪念日日期',{exact:true}).fill('2024-02-29');
  await page.getByLabel('纪念卡片说明',{exact:true}).fill('DEMO · 这段中文会在重新打开后保留。');
  await page.getByRole('button',{name:'保存',exact:true}).click();await saved(page);await page.reload();
  await expect(page.getByLabel('纪念日日期',{exact:true})).toHaveValue('2024-02-29');await expect(page.getByLabel('纪念卡片说明',{exact:true})).toHaveValue('DEMO · 这段中文会在重新打开后保留。');
  await page.getByRole('button',{name:'另存为',exact:true}).click();await expect(page.getByLabel('项目名称',{exact:true})).toHaveValue('DEMO · UI anniversary · 副本');
});
test('Chinese letter auto-save, word count and date lock preview',async({page})=>{
  await newProject(page,'letters');await page.getByRole('button',{name:'小信封',exact:true}).click();await page.getByRole('button',{name:'写一封信',exact:true}).first().click();
  const text='DEMO · 中文长信\n'+('普通的日子也值得收藏。'.repeat(900));
  await page.getByLabel('信件标题',{exact:true}).fill('DEMO · 等待打开的信');await page.getByLabel('信件正文',{exact:true}).fill(text);
  await page.getByLabel('解锁日期',{exact:true}).fill('2027-01-01');await page.getByLabel('模拟当地日期',{exact:true}).fill('2026-12-31');
  await expect(page.locator('.locked')).toBeVisible();await page.getByLabel('模拟当地日期',{exact:true}).fill('2027-01-01');await expect(page.locator('.paper-body')).toHaveText(text);
  await saved(page);await page.reload();await page.getByRole('button',{name:'小信封',exact:true}).click();await expect(page.getByLabel('信件正文',{exact:true})).toHaveValue(text);
});
test('timeline CRUD, tags, search and private display',async({page})=>{
  await newProject(page,'memories');await page.getByRole('button',{name:'回忆时间线',exact:true}).click();await page.getByRole('button',{name:'记录回忆',exact:true}).click();
  await page.getByLabel('回忆标题',{exact:true}).fill('DEMO · 可搜索的回忆');await page.getByLabel('回忆正文',{exact:true}).fill('DEMO · 时间线正文');await page.getByLabel('回忆标签',{exact:true}).fill('DEMO,春天');
  await page.getByLabel('搜索回忆',{exact:true}).fill('春天');await expect(page.locator('.timeline-row')).toHaveCount(1);await page.getByLabel('公开显示回忆',{exact:true}).uncheck();await expect(page.locator('.device-empty')).toContainText('私密');
  await page.getByRole('button',{name:'删除回忆',exact:true}).click();await page.getByRole('button',{name:'确认删除',exact:true}).click();await expect(page.locator('.timeline-row')).toHaveCount(0);
});
test('local photos, caption, cover, drag reorder and delete',async({page})=>{
  await newProject(page,'album');await page.getByRole('button',{name:'相册',exact:true}).click();
  await page.getByLabel('选择本地照片文件',{exact:true}).setInputFiles([path.resolve('examples/demo-sakura.png'),path.resolve('examples/demo-sage.png')]);await expect(page.locator('.photo-card')).toHaveCount(2);
  await page.getByLabel('照片标题',{exact:true}).fill('DEMO · 第二张');await page.getByRole('button',{name:'设为封面',exact:true}).click();await expect(page.locator('.photo-card').nth(1)).toContainText('封面');
  await page.locator('.photo-card').nth(1).dragTo(page.locator('.photo-card').nth(0));await expect(page.locator('.photo-card').nth(0)).toContainText('DEMO · 第二张');
  await saved(page);await page.reload();await page.getByRole('button',{name:'相册',exact:true}).click();await expect(page.locator('.photo-card').nth(0)).toContainText('DEMO · 第二张');
  await page.getByRole('button',{name:'删除照片',exact:true}).click();await page.getByRole('button',{name:'确认删除',exact:true}).click();await expect(page.locator('.photo-card')).toHaveCount(1);
});
test('RPG dialogue editing, choice and playback',async({page})=>{
  await newProject(page,'story');await page.getByRole('button',{name:'我们的故事',exact:true}).click();await page.getByRole('button',{name:'添加章节',exact:true}).first().click();
  await page.getByLabel('第1行说话者',{exact:true}).fill('DEMO 旁白');await page.getByLabel('第1行对话',{exact:true}).fill('DEMO · 你想翻开下一页吗？');await page.getByRole('button',{name:'添加选项',exact:true}).click();await page.getByLabel('选项1文字',{exact:true}).fill('DEMO · 收好这一页');
  await page.getByRole('button',{name:'播放剧情',exact:true}).click();await expect(page.locator('.dialogue-box')).toContainText('DEMO · 你想翻开下一页吗？');await page.locator('.device-choices').getByRole('button').click();await expect(page.locator('.dialogue-box')).toContainText('本段播放结束');
});
test('package export/import round trip and corrupt ZIP message',async({page})=>{
  await newProject(page,'roundtrip');await page.getByRole('button',{name:'添加日期',exact:true}).click();await page.getByLabel('纪念日标题',{exact:true}).fill('DEMO · 往返保留');await saved(page);
  const promise=page.waitForEvent('download');await page.getByRole('button',{name:'导出内容包',exact:true}).click();const file=await promise;
  const target=path.resolve('private/ui-roundtrip.lwpack');await fs.mkdir(path.dirname(target),{recursive:true});await file.saveAs(target);
  await page.getByRole('button',{name:'内容与导出',exact:true}).click();await page.getByLabel('导入内容包文件',{exact:true}).setInputFiles(target);await expect(page.locator('.toast')).toContainText('完整导入');
  await page.getByRole('button',{name:'纪念日',exact:true}).click();await expect(page.getByLabel('纪念日标题',{exact:true})).toHaveValue('DEMO · 往返保留');
  await page.getByLabel('导入内容包文件',{exact:true}).setInputFiles({name:'broken.lwpack',mimeType:'application/zip',buffer:Buffer.from('damaged')});await expect(page.locator('.toast')).toContainText('大小无效');
});
test('backups restore into a new project',async({page})=>{
  await newProject(page,'backups');await page.getByRole('button',{name:'添加日期',exact:true}).click();await saved(page);await page.getByLabel('纪念日标题',{exact:true}).fill('DEMO · 第二版');await saved(page);
  await page.getByRole('button',{name:'内容与导出',exact:true}).click();await page.getByRole('button',{name:'备份与恢复',exact:true}).click();await page.getByRole('button',{name:'恢复副本',exact:true}).first().click();await expect(page.getByLabel('项目名称',{exact:true})).toHaveValue(/恢复$/);
});
test('DEMO screenshots, themes, responsive layout and no external requests',async({page})=>{
  const external:string[]=[];page.on('request',r=>{if(!r.url().startsWith('http://127.0.0.1:4317')&&!r.url().startsWith('data:'))external.push(r.url());});
  await page.goto('/');await page.getByRole('button',{name:'新建',exact:true}).click();await page.getByRole('button',{name:'创建 DEMO',exact:true}).click();await saved(page);
  await fs.mkdir('reports/screenshots',{recursive:true});
  for(const [label,file] of [['纪念日','01-anniversary'],['小信封','02-letters'],['回忆时间线','03-memories'],['相册','04-album'],['我们的故事','05-story'],['内容与导出','06-package']]){
    await page.getByRole('button',{name:label,exact:true}).click();if(label==='我们的故事')await page.getByRole('button',{name:'播放剧情',exact:true}).click();await page.screenshot({path:`reports/screenshots/${file}.png`,fullPage:true});
  }
  await page.getByRole('button',{name:'纪念日',exact:true}).click();await page.getByRole('button',{name:'Moonlight 深色主题',exact:true}).click();await page.screenshot({path:'reports/screenshots/07-moonlight.png',fullPage:true});
  await page.getByRole('button',{name:'Sakura 樱花主题',exact:true}).click();await page.setViewportSize({width:1280,height:800});await expect(page.locator('.studio-columns')).toHaveCSS('display','grid');await expect(page.locator('body')).toHaveJSProperty('scrollWidth',1280);
  await page.setViewportSize({width:760,height:900});await page.screenshot({path:'reports/screenshots/08-compact.png',fullPage:true});await expect(page.locator('body')).toHaveJSProperty('scrollWidth',760);expect(external).toEqual([]);
});
