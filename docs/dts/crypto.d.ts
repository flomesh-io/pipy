/**
 * Public key in asymmetric cryptography.
 */
interface PublicKey {
}

interface PublicKeyConstructor {

  /**
   * Creates an instance of _PublicKey_.
   *
   * @param pem A string or a _Data_ object containing the public key in PEM format.
   * @param options Options including:
   *   - _aliasType_ - Can be `"sm2"` if the key is to be used in SM2 algorithm as ISO/IEC 14888.
   * @returns A _PublicKey_ object created from the PEM file.
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
   *
   * @param pem A string or a _Data_ object containing the public key in PEM format.
   * @param options Options including:
   *   - _aliasType_ - Can be `"sm2"` if the key is to be used in SM2 algorithm as ISO/IEC 14888.
   * @returns A _PrivateKey_ object created from the PEM file.
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

  /**
   * The subject of the certificate.
   */
  readonly subject: { [name: string]: string };

  /**
   * The issuer of the certificate.
   */
  readonly issuer: { [name: string]: string };
}

interface CertificateConstructor {

  /**
   * Creates an instance of _Certificate_.
   *
   * @param pem A string or a _Data_ object containing the certificate in PEM format.
   * @returns A _Certificate_ object created from the PEM file.
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
   *
   * @param pem A string or a _Data_ object containing the certificate chain in PEM format.
   * @returns A _CertificateChain_ object created from the PEM file.
   */
  new(pem: string | Data): CertificateChain;
}

/**
 * Encryption operation.
 */
interface Cipher {

  /**
   * Encrypts data.
   *
   * @param data A string or a _Data_ object containing the data to encrypt.
   * @returns A _Data_ object containing the encrypted data.
   */
  update(data: string | Data): Data;

  /**
   * Encrypts the remained data.
   *
   * @returns A _Data_ object containing the remained encrypted data.
   */
  final(): Data;
}

interface CipherConstructor {

  /**
   * Creates an instance of _Cipher_.
   *
   * @param algorithm A string containing the name of the encryption algorithm.
   * @param options Options including:
   *   - _key_ - A string or a _Data_ object containing the key for encryption.
   *   - _iv_ - A string or a _Data_ object containing the IV (Initialization Vector) for encryption.
   * @returns A _Cipher_ object using the specified algorithm and key.
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
   *
   * @param data A string or a _Data_ object containing the data to decrypt.
   * @returns A _Data_ object containing the decrypted data.
   */
  update(data: string | Data): Data;

  /**
   * Decrypts the remained data.
   *
   * @returns A _Data_ object containing the remained decrypted data.
   */
  final(): Data;
}

interface DecipherConstructor {

  /**
   * Creates an instance of _Decipher_.
   *
   * @param algorithm A string containing the name of the decryption algorithm.
   * @param options Options including:
   *   - _key_ - A string or a _Data_ object containing the key for decryption.
   *   - _iv_ - A string or a _Data_ object containing the IV (Initialization Vector) for decryption.
   * @returns A _Decipher_ object using the specified algorithm and key.
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
   *
   * @param data A string or a _Data_ object containing the data to digest.
   */
  update(data: string | Data): void;

  /**
   * Retrieves the final digest value.
   *
  * @returns A _Data_ object containing the digest.
   */
  digest(): Data;
}

interface HashConstructor {

  /**
   * Creates an instance of _Hash_.
   *
   * @param algorithm A string containing the name of the digest algorithm.
   * @returns A _Hash_ object using the specified algorithm.
   */
  new(algorithm: string): Hash;
}

/**
 * HMAC (Hash-based Message Authentication Code) calculation.
 */
interface Hmac {

  /**
   * Calculates HMAC.
   *
   * @param data A string or a _Data_ object containing the data to calculate HMAC for.
   */
  update(data: string | Data): void;

  /**
   * Retrieves the final HMAC value.
   *
   * @returns A _Data_ object containing the calculated HMAC.
   */
  digest(): Data;
}

interface HmacConstructor {

  /**
   * Creates an instance of _Hmac_.
   *
   * @param algorithm A string containing the name of the digest algorithm.
   * @param key A string or a _Data_ object containing the key for HMAC calculation.
   * @returns A _Hmac_ object using the specified digest algorithm and key.
   */
  new(algorithm: string, key: string | Data): Hmac;
}

/**
 * Signing operation.
 */
interface Sign {

  /**
   * Calculates digest.
   *
   * @param data A string or a _Data_ object containing the data to digest.
   */
  update(data: string | Data): void;

  /**
   * Calculates signature.
   *
   * @param key A _PrivateKey_ object containing the private key used in signing.
   * @param options Options including:
   *   - _id_ - A _Data_ object containing the identifier used by certain algorithms such as SM2.
   * @returns A _Data_ object containing the signature.
   */
  sign(key: PrivateKey, options?: { id?: Data }): Data;
}

interface SignConstructor {

  /**
   * Creates an instance of _Sign_.
   *
   * @param algorithm A string containing the name of the digest algorithm.
   * @returns A _Sign_ object using the specified digest algorithm.
   */
  new(algorithm: string): Sign;
}

/**
 * Signature verification.
 */
interface Verify {

  /**
   * Calculates digest.
   *
   * @param data A string or a _Data_ object containing the data to digest.
   */
  update(data: string | Data): void;

  /**
   * Verifies signature.
   *
   * @param key A _PublicKey_ object containing the public key used in verification.
   * @param signature A _Data_ object containing the signature to verify.
   * @param options Options including:
   *   - _id_ - A _Data_ object containing the identifier used by certain algorithms such as SM2.
   * @returns A boolean value indicating whether the signature is verified successfully.
   */
  verify(key: PublicKey, signature: Data, options?: { id?: Data }): boolean;
}

interface VerifyConstructor {

  /**
   * Creates an instance of _Verify_.
   *
   * @param algorithm A string containing the name of the digest algorithm.
   * @returns A _Verify_ object using the specified digest algorithm.
   */
  new(algorithm: string): Verify;
}

/**
 * JSON Web Key.
 */
interface JWK {

  /**
   * Whether the token is valid.
   */
  readonly isValid: boolean;
}

interface JWKConstructor {

  /**
   * Creates an instance of _JWK_.
   *
   * @param json An object containing the fields of a JWK.
   * @returns A _JWK_ object containing information of the JWK.
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
   *
   * @param key A _JWK_ object or a _PublicKey_ object containing the public key used in verification.
   * @returns A boolean value indicating whether the JWT is verified successfully.
   */
  verify(key: JWK | PublicKey): boolean;
}

interface JWTConstructor {

  /**
   * Creates an instace of _JWT_.
   *
   * @param token A string containing the Base64-encoded JWT.
   * @returns A _JWT_ object containing information of the JWT.
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
