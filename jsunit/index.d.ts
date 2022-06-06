/*
 * Copyright (c) 2021-2022 Huawei Device Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License")
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

export const DEFAULT = 0B0000

export enum TestType {
    FUNCTION = 0B1,
    PERFORMANCE = 0B1 << 1,
    POWER = 0B1 << 2,
    RELIABILITY = 0B1 << 3,
    SECURITY = 0B1 << 4,
    GLOBAL = 0B1 << 5,
    COMPATIBILITY = 0B1 << 6,
    USER = 0B1 << 7,
    STANDARD = 0B1 << 8,
    SAFETY = 0B1 << 9,
    RESILIENCE = 0B1 << 10
}

export enum Size {
    SMALLTEST = 0B1 << 16,
    MEDIUMTEST = 0B1 << 17,
    LARGETEST = 0B1 << 18
}

export enum Level {
    LEVEL0 = 0B1 << 24,
    LEVEL1 = 0B1 << 25,
    LEVEL2 = 0B1 << 26,
    LEVEL3 = 0B1 << 27,
    LEVEL4 = 0B1 << 28
}

export function describe(testSuiteName: string, callback: Function): void

export function beforeEach(callback: Function): void

export function afterEach(callback: Function): void

export function beforeAll(callback: Function): void

export function afterAll(callback: Function): void

export function it(testCaseName: string, attribute: (TestType | Size | Level), callback: Function)

export interface Assert {
    assertClose(expectValue: number, precision: number): void
    assertContain(expectValue: any): void
    assertEqual(expectValue: any): void
    assertFail(): void
    assertFalse(): void
    assertTrue(): void
    assertInstanceOf(expectValue: string): void
    assertLarger(expectValue: number): void
    assertLess(expectValue: number): void
    assertNull(): void
    assertThrowError(expectValue: string): void
    assertUndefined(): void
}

export function expect(actualValue?: any): Assert

export class SysTestKit {
    static existKeyword(keyword: string, timeout?: number): boolean
}

export class Hypium {
    static setData(data: {[key: string]: any}): void
    static hypiumTest(abilityDelegator: any, abilityDelegatorArguments: any, testsuite: Function): void
}