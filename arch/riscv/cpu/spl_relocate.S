/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * SMP 安全的 SPL 自搬运代码
 *
 * Copyright (C) 2025
 *
 * 此文件提供位置无关代码，允许 SPL 将自己从 Boot ROM 加载地址
 * 搬运到链接地址。
 *
 * 针对 JH7110：
 *   - Boot ROM 将 SPL 加载到 0x8000000
 *   - SPL 链接地址为 0x8080000
 *   - 此代码将 SPL 从 0x8000000 搬运到 0x8080000
 *   - 搬运后，0x8000000~0x8080000 区域被释放
 *
 * SMP 支持：
 *   - 只有 hart 0 执行搬运
 *   - 其他 hart 等待搬运完成
 *   - 所有 hart 最终都跳转到搬运后的地址
 */

#include <asm-offsets.h>
#include <config.h>
#include <system-constants.h>
#include <asm/encoding.h>

#ifdef CONFIG_32BIT
#define REGBYTES	4
#else
#define REGBYTES	8
#endif

	.section .text.spl_relocate, "ax"
	.globl _spl_relocate_start
_spl_relocate_start:

	/*
	 * SMP 安全的 SPL 自搬运入口点
	 *
	 * 只有 hart 0 执行搬运，其他 hart 等待
	 * 所有 hart 最终都跳转到搬运后的地址
	 */

	/* 获取当前 _spl_relocate_start 的实际运行地址（位置无关） */
.Lget_pc:
	auipc	t0, %pcrel_hi(_spl_relocate_start)
	addi	t0, t0, %pcrel_lo(.Lget_pc)
	/* t0 = 实际 _spl_relocate_start 地址 */

	/* 保存源地址供后续使用 */
	mv	t5, t0
	
	/* 获取当前 hart ID */
	csrr	tp, mhartid		/* tp = hart ID */

	/* 加载目标地址并检查是否需要搬运 */
	li	t1, CONFIG_SPL_TEXT_BASE
	beq	t0, t1, .Lalready_relocated	/* 已在目标地址 */

	/* 计算同步标志地址（固定在 CONFIG_SPL_TEXT_BASE - 8） */
	addi	t6, t1, -8			/* t6 = 标志地址 */

	sw	zero, 0(t6)
	fence					/* 确保初始化对其他 hart 可见 */

	/* hart0 初始化标志为 0，确保其他 hart 看到的是 0 */
	bnez	tp, .Lwait_for_relocate

	/* === hart0 执行搬运 === */

	/* 计算镜像大小 */
	li	t2, CONFIG_SPL_MAX_SIZE
	mv	t3, t5				/* t3 = 源指针 */
	mv	t4, t1				/* t4 = 目标指针 */
	add	t0, t5, t2			/* t0 = 源结束地址 */

.Lcopy_loop:
	ld	s0, 0(t3)
	sd	s0, 0(t4)
	addi	t3, t3, 8
	addi	t4, t4, 8
	bltu	t3, t0, .Lcopy_loop

	/* 刷新指令缓存 */
	fence.i

	/* 设置搬运完成标志 */
	li	t0, 1
	sw	t0, 0(t6)
	fence					/* 确保标志对其他 hart 可见 */

	/* 跳转到搬运后的代码 */
	li	t0, CONFIG_SPL_TEXT_BASE
	jr	t0

.Lwait_for_relocate:
	/* 非 hart0 等待 hart0 完成搬运 */
	lw	t0, 0(t6)
	fence					/* 确保读取可见性 */
	beqz	t0, .Lwait_for_relocate	/* 标志未设置，继续等待 */

	/* 搬运完成，跳转到新地址 */
	li	t0, CONFIG_SPL_TEXT_BASE
	jr	t0

.Lalready_relocated:
	/* 已经在目标地址，继续正常的 SPL 代码 */
	.globl _spl_relocate_end
_spl_relocate_end:
	.previous
