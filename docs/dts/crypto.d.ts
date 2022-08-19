/// <reference no-default-lib="true"/>

/**
 * Public key in asymmetric cryptography.
 */
interface PublicKey {
}

interface PublicKeyConstructor {

  /**
   * Creates an instance of _PublicKey_.
   */
  new(
    pem: string | Data,
    options?: { aliasType?: 'sm2' }
  ): PublicKey;
}

/**
 * Private key in asymmetric cryptography.
 */
interface PrivateKey {
}

interface PrivateKeyConstructor {

  /**
   * Creates an instance of _PrivateKey_.
   */
  new(
    pem: string | Data,
    options?: { aliasType?: 'sm2' }
  ): PrivateKey;
}

/**
 * Certificate in X.509 standard.
 */
interface Certificate {
}

interface CertificateConstructor {

  /**
   * Creates an instance of _Certificate_.
   */
  new(pem: string | Data): Certificate;
}

/**
 * Certificate chain in X.509 standard.
 */
interface CertificateChain {
}

interface CertificateChainConstructor {

  /**
   * Creates an instance of _CertificateChain_.
   */
  new(pem: string | Data): CertificateChain;
}

/**
 * Encryption operation.
 */
interface Cipher {

  /**
   * Encrypts data.
   */
  update(data: string | Data): Data;

  /**
   * Encrypts the remained data.
   */
  final(): Data;
}

interface CipherConstructor {

  /**
   * Creates an instance of _Cipher_.
   */
  new(
    algorithm: string,
    options: { key: string | Data, iv?: string | Data }
  ): Cipher;
}

/**
 * Decryption operation.
 */
interface Decipher {

  /**
   * Decrypts data.
   */
  update(data: string | Data): Data;

  /**
   * Decrypts the remained data.
   */
  final(): Data;
}

interface DecipherConstructor {

  /**
   * Creates an instance of _Decipher_.
   */
  new(
    algorithm: string,
    options: { key: string | Data, iv?: string | Data }
  ): Decipher;
}

/**
 * Digest calculation.
 */
interface Hash {

  /**
   * Calculates digest.
   */
  update(data: string | Data, encoding?: 'utf8' | 'hex' | 'base64' | 'base64url'): void;

  /**
   * Retrieves the final digest value.
   */
  digest(encoding?: 'utf8' | 'hex' | 'base64' | 'base64url'): Data | string;
}

interface HashConstructor {

  /**
   * Creates an instance of _Hash_.
   */
  new(algorithm: string): Hash;
}

/**
 * HMAC (Hash-based Message Authentication Code) calculation.
 */
interface Hmac {

  /**
   * Calculates HMAC.
   */
  update(data: string | Data, encoding?: 'utf8' | 'hex' | 'base64' | 'base64url'): void;

  /**
   * Retrieves the final HMAC value.
   */
  digest(encoding?: 'utf8' | 'hex' | 'base64' | 'base64url'): Data | string;
}

interface HmacConstructor {

  /**
   * Creates an instance of _Hmac_.
   */
  new(algorithm: string, key: string | Data): Hmac;
}

/**
 * Signing operation.
 */
interface Sign {

  /**
   * Calculates digest.
   */
  update(data: string | Data, encoding?: 'utf8' | 'hex' | 'base64' | 'base64url'): void;

  /**
   * Calculates signature.
   */
  sign(key: PrivateKey, options?: { id?: Data }): Data;
  sign(key: PrivateKey, encoding: 'utf8' | 'hex' | 'base64' | 'base64url', options?: { id?: Data }): string;
}

interface SignConstructor {

  /**
   * Creates an instance of _Sign_.
   */
  new(algorithm: string): Sign;
}

/**
 * Signature verification.
 */
interface Verify {

  /**
   * Calculates digest.
   */
  update(data: string | Data, encoding?: 'utf8' | 'hex' | 'base64' | 'base64url'): void;

  /**
   * Verifies signature.
   */
  verify(key: PublicKey, signature: Data, options?: { id?: Data }): boolean;
  verify(key: PublicKey, signature: string, encoding: 'utf8' | 'hex' | 'base64' | 'base64url', options?: { id?: Data }): boolean;
}

interface VerifyConstructor {

  /**
   * Creates an instance of _Verify_.
   */
  new(algorithm: string): Verify;
}

/**
 * JSON Web Key.
 */
interface JWK {
}

interface JWKConstructor {

  /**
   * Creates an instance of _JWK_.
   */
  new(json: object): JWK;
}

/**
 * JSON Web Token.
 */
interface JWT {

  /**
   * Whether the token is valid.
   */
  readonly isValid: boolean;

  /**
   * Header part of the token.
   */
  readonly header: object;

  /**
   * Payload part of the token.
   */
  readonly payload: object;

  /**
   * Verifies the token.
   */
  verify(key: JWK | PublicKey | Data | string): boolean;
}

interface JWTConstructor {

  /**
   * Creates an instace of _JWT_.
   */
  new(token: string): JWT;
}

interface Crypto {
  PublicKey: PublicKeyConstructor,
  PrivateKey: PrivateKeyConstructor,
  Certificate: CertificateConstructor,
  CertificateChain: CertificateChainConstructor,
  Cipher: CipherConstructor,
  Decipher: DecipherConstructor,
  Hash: HashConstructor,
  Hmac: HmacConstructor,
  Sign: SignConstructor,
  Verify: VerifyConstructor,
  JWK: JWKConstructor,
  JWT: JWTConstructor,
}

declare var crypto: Crypto;
