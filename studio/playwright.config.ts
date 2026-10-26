import { defineConfig } from '@playwright/test';
export default defineConfig({
  testDir:'./tests/ui',fullyParallel:false,workers:1,timeout:30000,
  reporter:[['list']],use:{baseURL:'http://127.0.0.1:4317',channel:'msedge',headless:true,viewport:{width:1600,height:1000},acceptDownloads:true},
  webServer:{command:'node server/index.ts',url:'http://127.0.0.1:4317',reuseExistingServer:true,env:{LW_STUDIO_DATA:'private/e2e-projects'}},
});
