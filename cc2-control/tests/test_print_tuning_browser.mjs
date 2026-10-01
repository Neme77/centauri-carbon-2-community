// Optional real-browser test: see docs/PRINT_TUNING.md for setup.
import assert from "node:assert/strict";
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";
const webRoot = path.resolve(
	path.dirname(fileURLToPath(import.meta.url)),
	"../web",
);
import http from "node:http";
const server = http.createServer((req, res) => {
	const name = req.url.startsWith("/i18n/")
		? "locales/" + path.basename(req.url)
		: "index.html";
	res.setHeader(
		"Content-Type",
		name.endsWith(".json") ? "application/json" : "text/html",
	);
	res.end(fs.readFileSync(path.join(webRoot, name)));
});
await new Promise((resolve) => server.listen(0, "127.0.0.1", resolve));
const { chromium: playwright } = await import(
	process.env.CC2_PLAYWRIGHT_MODULE || "playwright"
);
const browser = await playwright.launch({
	headless: true,
	...(process.env.CC2_BROWSER_PATH
		? { executablePath: process.env.CC2_BROWSER_PATH }
		: {}),
	args: [
		"--no-sandbox",
		"--disable-gpu",
		"--disable-software-rasterizer",
		"--single-process",
		"--no-zygote",
	],
});
const page = await browser.newPage({ viewport: { width: 390, height: 844 } });
const errors = [];
page.on("pageerror", (e) => errors.push(e.message));
let d = {
	connected: true,
	last_message_age: 0,
	machine: { status: 2, status_name: "Printing", progress: 20 },
	print: {
		filename: "Buddha.gcode",
		uuid: "test-job",
		state: "printing",
		current_layer: 20,
		total_layers: 227,
		duration: 120,
		remaining: 1200,
	},
	extruder: { temperature: 210, target: 210 },
	heater_bed: { temperature: 60, target: 60 },
	chamber: { temperature: 28 },
	fans: { part: 153, aux: 0, box: 0 },
	hardware: { camera: false, light: 1 },
	motion: { x: 20, y: 20, z: 4, homed_axes: "xyz" },
	tuning: { speed_percent: 100, flow_percent: 100, live_velocity: 80 },
};
const longFilename =
	"ECC2_0.4_Buddha_Elegoo_PLA_very_long_print_name_with_extended_metadata_0.2_25m47s.gcode";
const commands = [];
let failPrinter = false;
await page.route("**/api/**", async (route) => {
	const req = route.request(),
		path = new URL(req.url()).pathname;
	let out = {};
	if (path === "/api/printer") {
		if (failPrinter)
			return route.fulfill({
				status: 503,
				contentType: "application/json",
				body: '{"error":"offline fixture"}',
			});
		out = d;
	}
	if (path === "/api/health")
		out = { version: "test", mqtt_connected: true, mqtt_registered: true };
	if (path === "/api/setup") out = { required: false, configured: true };
	if (path === "/api/preferences") out = { language: "en", theme: "dark" };
	if (path === "/api/orca/pending-print") out = { pending: false };
	if (path === "/api/material-presets") out = { presets: [] };
	if (path === "/api/gcode-files")
		out = {
			internal: { files: [{ path: longFilename, size: 5190855, modified: 1 }] },
			usb: { files: [] },
		};
	if (path === "/api/control") {
		const command = req.postData();
		commands.push(command);
		const [, kind, value] = command.split(":");
		if (command.startsWith("tune:"))
			setTimeout(() => {
				d.tuning[kind + "_percent"] = Number(value);
			}, 200);
		return route.fulfill({
			status: 202,
			contentType: "application/json",
			body: '{"accepted":true}',
		});
	}
	return route.fulfill({
		contentType: "application/json",
		body: JSON.stringify(out),
	});
});
await page.goto(`http://127.0.0.1:${server.address().port}`);
await page.locator("#tune-speed").waitFor();
const overflow = async () =>
	page.evaluate(() => ({
		width: innerWidth,
		scroll: document.documentElement.scrollWidth,
		main: document.querySelector("main").getBoundingClientRect().x,
	}));
let size = await overflow();
assert.equal(size.main, 0);
assert.ok(size.scroll <= size.width, JSON.stringify(size));
assert.equal(await page.locator("#cc2-navigation").isVisible(), false);
await page.getByRole("button", { name: "Expand menu", exact: true }).click();
assert.equal(await page.locator("#cc2-navigation").isVisible(), true);
await page.waitForFunction(() =>
	document.activeElement?.matches("#cc2-navigation a"),
);
await page.keyboard.press("Escape");
await page.locator("#cc2-navigation").waitFor({ state: "hidden" });
await page.getByRole("button", { name: "Expand menu", exact: true }).click();
await page.locator('#cc2-navigation a[href="#job"]').click();
await page.locator("#cc2-navigation").waitFor({ state: "hidden" });
await page.locator("#tune-speed").fill("77");
await page.waitForTimeout(1800);
assert.equal(await page.locator("#tune-speed").inputValue(), "77");
await page.locator("#tune-speed").fill("201");
assert.equal(
	await page
		.locator("form")
		.filter({ has: page.locator("#tune-speed") })
		.getByRole("button", { name: "Apply", exact: true })
		.isDisabled(),
	true,
);
await page.locator("#tune-speed").fill("80");
await page.locator("#tune-speed").press("Enter");
assert.equal(await page.locator("#tune-flow").isDisabled(), true);
await page.waitForFunction(
	() => !document.querySelector("#tune-speed").disabled,
);
assert.deepEqual(commands, ["tune:speed:80"]);
assert.equal(await page.locator("#tune-speed").inputValue(), "");
await page.locator("#tune-flow").fill("95");
await page
	.locator("form")
	.filter({ has: page.locator("#tune-flow") })
	.getByRole("button", { name: "Apply", exact: true })
	.click();
await page.waitForFunction(
	() => !document.querySelector("#tune-flow").disabled,
);
await page
	.locator("form")
	.filter({ has: page.locator("#tune-flow") })
	.getByRole("button", { name: "Reset 100%", exact: true })
	.click();
await page.waitForFunction(
	() => !document.querySelector("#tune-flow").disabled,
);
assert.deepEqual(commands, ["tune:speed:80", "tune:flow:95", "tune:flow:100"]);
await page.evaluate(() => scrollTo(0, 0));
if (process.env.CC2_SCREENSHOTS)
	await page.screenshot({
		path: path.join(process.env.CC2_SCREENSHOTS, "portrait-job.png"),
		fullPage: true,
	});
for (const width of [320, 360, 390]) {
	await page.setViewportSize({ width, height: 844 });
	for (const name of [
		"dashboard",
		"control",
		"job",
		"files",
		"bed",
		"canvas",
		"console",
		"settings",
	]) {
		await page.evaluate((name) => (location.hash = name), name);
		await page.waitForTimeout(180);
		if (name === "files") {
			const button = page.locator(".cc2-file-name");
			await button.waitFor();
			const metrics = await button.evaluate((el) => ({
				height: el.getBoundingClientRect().height,
				line: parseFloat(getComputedStyle(el).lineHeight),
				whiteSpace: getComputedStyle(el).whiteSpace,
				overflow: getComputedStyle(el).textOverflow,
				title: el.title,
			}));
			assert.equal(metrics.whiteSpace, "nowrap");
			assert.equal(metrics.overflow, "ellipsis");
			assert.equal(metrics.title, longFilename);
			assert.ok(metrics.height <= metrics.line + 1);
			await button.click();
			await page.locator(".cc2-file-expanded").waitFor();
			assert.equal(
				await page.locator(".cc2-file-expanded").textContent(),
				longFilename,
			);
		}
		size = await overflow();
		assert.ok(
			size.scroll <= width,
			`Overflow ${width} ${name}: ${JSON.stringify(size)}`,
		);
	}
}
await page.setViewportSize({ width: 844, height: 390 });
await page.evaluate(() => (location.hash = "dashboard"));
await page.waitForTimeout(200);
size = await overflow();
assert.equal(size.main, 208);
assert.equal(
	await page
		.getByRole("button", { name: "Expand menu", exact: true })
		.isVisible(),
	false,
);
if (process.env.CC2_SCREENSHOTS)
	await page.screenshot({
		path: path.join(process.env.CC2_SCREENSHOTS, "landscape.png"),
		fullPage: true,
	});
await page.setViewportSize({ width: 740, height: 360 });
await page.waitForTimeout(250);
size = await overflow();
assert.equal(size.main, 74);
failPrinter = true;
await page.waitForTimeout(2000);
assert.equal(await page.locator("#tune-speed").isDisabled(), true);
failPrinter = false;
await page.waitForTimeout(2000);
assert.equal(await page.locator("#tune-speed").isDisabled(), false);
d.tuning.speed_percent = null;
await page.waitForTimeout(1800);
assert.equal(await page.locator("#tune-speed").isDisabled(), true);
d.tuning.speed_percent = 100;
d.machine.status = 1;
d.machine.status_name = "Idle";
d.print = { state: "complete", filename: "", uuid: "" };
await page.waitForTimeout(1800);
assert.equal(await page.locator("#tune-flow").isDisabled(), true);
assert.deepEqual(errors, []);
console.log(
	"PASS browser tuning input, Enter/apply/reset, readback, duplicate guard, stale/idle and portrait navigation; landscape rail preserved",
);
await browser.close();
server.close();
