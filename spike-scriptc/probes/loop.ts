const t0 = Date.now();
let acc = 0;
for (let i = 0; i < 20_000_000; i++) { acc = (acc + i) % 1000003; }
console.log(`loop 20M: ${Date.now() - t0}ms acc=${acc}`);
const t1 = Date.now();
const big: { idx: number; text: string }[] = [];
for (let i = 0; i < 40000; i++) big.push({ idx: i, text: "transcript line " + i + " lorem ipsum dolor sit amet consectetur" });
const s = JSON.stringify(big);
console.log(`stringify ${Math.round(s.length / 1024)}KB: ${Date.now() - t1}ms`);
const t2 = Date.now();
const parsed = JSON.parse(s) as { idx: number; text: string }[];
console.log(`parse: ${Date.now() - t2}ms n=${parsed.length}`);
